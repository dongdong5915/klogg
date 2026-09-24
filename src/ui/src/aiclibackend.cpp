/*
 * Copyright (C) 2026 klogg contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "aiclibackend.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>

#include <utility>

namespace {

constexpr int MaxPromptBytes = 300 * 1024;
constexpr int MaxSchemaBytes = 64 * 1024;
constexpr int MaxStdoutBytes = 1024 * 1024;
constexpr int MaxStderrBytes = 64 * 1024;
constexpr int RequestTimeoutMs = 120 * 1000;

QString codeBuddyPath()
{
    const QString path = QStandardPaths::findExecutable( QStringLiteral( "codebuddy" ) );
    return QFileInfo( path ).canonicalFilePath();
}

QString bubblewrapPath()
{
    return QStandardPaths::findExecutable( QStringLiteral( "bwrap" ) );
}

QString authDirectory()
{
    return QDir::homePath() + QStringLiteral( "/.codebuddy/local_storage" );
}

// CodeBuddy keeps its cloud credentials in the IDE extension data dir, not under ~/.codebuddy.
QString extensionAuthDirectory()
{
    return QDir::homePath()
           + QStringLiteral( "/.local/share/CodeBuddyExtension/Data/Public/auth" );
}

QString providerName( AiCliBackend::Provider provider )
{
    return provider == AiCliBackend::Provider::Codex ? QStringLiteral( "Codex" )
                                                     : QStringLiteral( "CodeBuddy" );
}

} // namespace

AiCliBackend::AiCliBackend( QObject* parent )
    : QObject( parent )
    , process_( this )
    , timeout_( this )
{
    timeout_.setSingleShot( true );

    connect( &process_, &QProcess::started, this, [ this ] {
        if ( active_ ) {
            process_.write( prompt_ );
            process_.closeWriteChannel();
            prompt_.clear();
        }
    } );
    connect( &process_, &QProcess::readyReadStandardOutput, this, &AiCliBackend::readStdout );
    connect( &process_, &QProcess::readyReadStandardError, this, &AiCliBackend::readStderr );
    connect( &process_, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), this,
             [ this ]( int exitCode, QProcess::ExitStatus exitStatus ) {
                 if ( !active_ ) {
                     return;
                 }

                 readStdout();
                 readStderr();
                 if ( !active_ ) {
                     return;
                 }

                 timeout_.stop();
                 active_ = false;
                 if ( stdout_.startsWith( "Authentication required" ) ) {
                     Q_EMIT failed( provider_,
                                    tr( "%1 needs sign-in. Run codebuddy /login in a terminal." )
                                        .arg( providerName( provider_ ) ) );
                 }
                 else if ( exitStatus != QProcess::NormalExit || exitCode != 0 ) {
                     Q_EMIT failed( provider_, tr( "%1 exited with code %2." )
                                                   .arg( providerName( provider_ ) )
                                                   .arg( exitCode ) );
                 }
                 else {
                     Q_EMIT finished( provider_, QString::fromUtf8( stdout_ ) );
                 }

                 stdout_.clear();
                 stderr_.clear();
             } );
    connect( &process_, &QProcess::errorOccurred, this, [ this ]( QProcess::ProcessError ) {
        if ( active_ ) {
            fail( tr( "Could not start the isolated %1 process: %2" )
                      .arg( providerName( provider_ ), process_.errorString() ) );
        }
    } );
    connect( &timeout_, &QTimer::timeout, this, [ this ] {
        fail( tr( "%1 request timed out after %2 seconds." )
                  .arg( providerName( provider_ ) )
                  .arg( RequestTimeoutMs / 1000 ) );
    } );
}

AiCliBackend::~AiCliBackend()
{
    if ( process_.state() != QProcess::NotRunning ) {
        process_.kill();
        process_.waitForFinished( 1000 );
    }
}

QString AiCliBackend::availability( Provider provider ) const
{
    if ( provider == Provider::Codex ) {
        return tr( "Codex CLI cannot disable model tools, so its credential files cannot be isolated safely." );
    }
    if ( provider != Provider::CodeBuddy ) {
        return tr( "Unknown AI provider." );
    }
    if ( bubblewrapPath().isEmpty() ) {
        return tr( "bubblewrap (bwrap) is required to isolate CodeBuddy from local files." );
    }
    if ( codeBuddyPath().isEmpty() ) {
        return tr( "CodeBuddy CLI was not found in PATH." );
    }
    if ( !QDir( authDirectory() ).exists() || !QDir( extensionAuthDirectory() ).exists() ) {
        return tr( "CodeBuddy login data is unavailable. Run codebuddy /login in a terminal." );
    }
    if ( !QDir::homePath().startsWith( QStringLiteral( "/home/" ) ) ||
         !QFileInfo( QStringLiteral( "/usr" ) ).isDir() ||
         !QFileInfo( QStringLiteral( "/etc/ssl" ) ).isDir() ||
         !QFileInfo( QStringLiteral( "/etc/hosts" ) ).isFile() ||
         QFileInfo( QStringLiteral( "/etc/resolv.conf" ) ).canonicalFilePath().isEmpty() ) {
        return tr( "This Linux runtime does not provide the paths required for CodeBuddy isolation." );
    }

    return {};
}

bool AiCliBackend::start( Provider provider, const QString& prompt, const QByteArray& schema )
{
    if ( active_ || process_.state() != QProcess::NotRunning ) {
        Q_EMIT failed( provider, tr( "An AI request is already running or stopping." ) );
        return false;
    }

    const QString unavailable = availability( provider );
    if ( !unavailable.isEmpty() ) {
        Q_EMIT failed( provider, unavailable );
        return false;
    }

    const QByteArray input = prompt.toUtf8();
    if ( input.isEmpty() || input.size() > MaxPromptBytes || schema.isEmpty() ||
         schema.size() > MaxSchemaBytes ) {
        Q_EMIT failed( provider, tr( "AI request exceeds its prompt or schema limit." ) );
        return false;
    }

    const QString home = QDir::homePath();
    const QString userState = home + QStringLiteral( "/.codebuddy/user-state.json" );
    const QString dnsConfig = QFileInfo( QStringLiteral( "/etc/resolv.conf" ) ).canonicalFilePath();
    const QString extensionAuth = QFileInfo( extensionAuthDirectory() ).canonicalFilePath();
    QStringList args = {
        QStringLiteral( "--die-with-parent" ),
        QStringLiteral( "--unshare-pid" ),
        QStringLiteral( "--unshare-ipc" ),
        QStringLiteral( "--ro-bind" ), QStringLiteral( "/usr" ), QStringLiteral( "/usr" ),
        QStringLiteral( "--symlink" ), QStringLiteral( "usr/lib64" ), QStringLiteral( "/lib64" ),
        QStringLiteral( "--symlink" ), QStringLiteral( "usr/lib" ), QStringLiteral( "/lib" ),
        QStringLiteral( "--proc" ), QStringLiteral( "/proc" ),
        QStringLiteral( "--dev" ), QStringLiteral( "/dev" ),
        QStringLiteral( "--tmpfs" ), QStringLiteral( "/tmp" ),
        QStringLiteral( "--tmpfs" ), QStringLiteral( "/home" ),
        QStringLiteral( "--dir" ), home,
        QStringLiteral( "--dir" ), home + QStringLiteral( "/.codebuddy" ),
        QStringLiteral( "--ro-bind" ), authDirectory(), authDirectory(),
        QStringLiteral( "--dir" ), home + QStringLiteral( "/.local" ),
        QStringLiteral( "--dir" ), home + QStringLiteral( "/.local/share" ),
        QStringLiteral( "--dir" ), home + QStringLiteral( "/.local/share/CodeBuddyExtension" ),
        QStringLiteral( "--dir" ),
        home + QStringLiteral( "/.local/share/CodeBuddyExtension/Data" ),
        QStringLiteral( "--dir" ),
        home + QStringLiteral( "/.local/share/CodeBuddyExtension/Data/Public" ),
        QStringLiteral( "--ro-bind" ), extensionAuth, extensionAuth,
        QStringLiteral( "--dir" ), QStringLiteral( "/opt" ),
        QStringLiteral( "--ro-bind" ), codeBuddyPath(), QStringLiteral( "/opt/codebuddy" ),
        QStringLiteral( "--dir" ), QStringLiteral( "/etc" ),
        QStringLiteral( "--ro-bind" ), QStringLiteral( "/etc/ssl" ), QStringLiteral( "/etc/ssl" ),
        QStringLiteral( "--ro-bind" ), QStringLiteral( "/etc/hosts" ), QStringLiteral( "/etc/hosts" ),
        QStringLiteral( "--ro-bind" ), dnsConfig, QStringLiteral( "/etc/resolv.conf" ),
        QStringLiteral( "--clearenv" ),
        QStringLiteral( "--setenv" ), QStringLiteral( "HOME" ), home,
        QStringLiteral( "--setenv" ), QStringLiteral( "PATH" ), QStringLiteral( "/usr/bin:/bin" ),
        QStringLiteral( "--setenv" ), QStringLiteral( "LANG" ), QStringLiteral( "C.UTF-8" ),
        QStringLiteral( "--chdir" ), QStringLiteral( "/tmp" ),
        QStringLiteral( "--" ), QStringLiteral( "/opt/codebuddy" ),
        QStringLiteral( "--print" ),
        // Only the structured output tool. Passing "" also removes StructuredOutput, which
        // silently disables --json-schema.
        QStringLiteral( "--tools" ), QStringLiteral( "StructuredOutput" ),
        QStringLiteral( "--strict-mcp-config" ),
        QStringLiteral( "--mcp-config" ), QStringLiteral( "{}" ),
        QStringLiteral( "--no-session-persistence" ),
        QStringLiteral( "--output-format" ), QStringLiteral( "json" ),
        QStringLiteral( "--json-schema" ), QString::fromUtf8( schema ),
        QStringLiteral( "--max-turns" ), QStringLiteral( "1" )
    };
    if ( QFileInfo( userState ).isFile() ) {
        const int insertAt = args.indexOf( QStringLiteral( "--dir" ),
                                           args.indexOf( authDirectory() ) );
        args.insert( insertAt, QStringLiteral( "--ro-bind" ) );
        args.insert( insertAt + 1, userState );
        args.insert( insertAt + 2, userState );
    }

    provider_ = provider;
    prompt_ = input;
    stdout_.clear();
    stderr_.clear();
    active_ = true;
    process_.setProgram( bubblewrapPath() );
    process_.setArguments( args );
    process_.setWorkingDirectory( QStringLiteral( "/tmp" ) );
    process_.start();
    timeout_.start( RequestTimeoutMs );
    return true;
}

void AiCliBackend::cancel()
{
    if ( !active_ ) {
        return;
    }

    active_ = false;
    timeout_.stop();
    prompt_.clear();
    stdout_.clear();
    stderr_.clear();
    if ( process_.state() != QProcess::NotRunning ) {
        process_.kill();
    }
    Q_EMIT cancelled();
}

void AiCliBackend::readStdout()
{
    stdout_ += process_.readAllStandardOutput();
    if ( stdout_.size() > MaxStdoutBytes ) {
        fail( tr( "%1 output exceeded the %2 KiB limit." )
                  .arg( providerName( provider_ ) )
                  .arg( MaxStdoutBytes / 1024 ) );
    }
}

void AiCliBackend::readStderr()
{
    stderr_ += process_.readAllStandardError();
    if ( stderr_.size() > MaxStderrBytes ) {
        fail( tr( "%1 diagnostic output exceeded the %2 KiB limit." )
                  .arg( providerName( provider_ ) )
                  .arg( MaxStderrBytes / 1024 ) );
    }
}

void AiCliBackend::fail( QString message )
{
    if ( !active_ ) {
        return;
    }

    active_ = false;
    timeout_.stop();
    prompt_.clear();
    stdout_.clear();
    stderr_.clear();
    if ( process_.state() != QProcess::NotRunning ) {
        process_.kill();
    }
    Q_EMIT failed( provider_, std::move( message ) );
}
