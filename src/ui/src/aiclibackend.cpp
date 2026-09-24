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

#include "configuration.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

#include <utility>

namespace {

constexpr int MaxPromptBytes = 300 * 1024;
constexpr int MaxSchemaBytes = 64 * 1024;
constexpr int MaxStdoutBytes = 1024 * 1024;
constexpr int MaxStderrBytes = 64 * 1024;
constexpr int DefaultTimeoutMs = 120000;

QString providerExecutableName( AiCliBackend::Provider provider )
{
    if ( provider == AiCliBackend::Provider::Codex ) {
        return QStringLiteral( "codex" );
    }
    return QStringLiteral( "codebuddy" );
}

QString providerAuthDirectory()
{
    return QDir::homePath() + QStringLiteral( "/.codebuddy/local_storage" );
}

// CodeBuddy keeps its cloud credentials in the IDE extension data dir, not under ~/.codebuddy.
QString providerExtensionAuthDirectory()
{
    return QDir::homePath()
           + QStringLiteral( "/.local/share/CodeBuddyExtension/Data/Public/auth" );
}

QString bubblewrapPath()
{
    return QStandardPaths::findExecutable( QStringLiteral( "bwrap" ) );
}

QString resolveExecutable( AiCliBackend::Provider provider,
                           const AiProviderSettings& settings )
{
    if ( !settings.executablePath.isEmpty() ) {
        const QFileInfo info( settings.executablePath );
        if ( info.isFile() && info.isExecutable() ) {
            return info.canonicalFilePath();
        }
    }

    const QString path = QStandardPaths::findExecutable( providerExecutableName( provider ) );
    return QFileInfo( path ).canonicalFilePath();
}

QStringList splitArgs( const QString& args )
{
    if ( args.isEmpty() ) {
        return {};
    }

    return args.split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
}

QStringList providerArguments( AiCliBackend::Provider provider, const QByteArray& schema,
                               const AiProviderSettings& settings )
{
    QStringList args;

    if ( provider == AiCliBackend::Provider::CodeBuddy ) {
        args << QStringLiteral( "--print" )
             << QStringLiteral( "--tools" )
             << QStringLiteral( "StructuredOutput" )
             << QStringLiteral( "--strict-mcp-config" )
             << QStringLiteral( "--mcp-config" )
             << QStringLiteral( "{}" )
             << QStringLiteral( "--no-session-persistence" );
    }

    args << QStringLiteral( "--output-format" )
         << QStringLiteral( "json" )
         << QStringLiteral( "--json-schema" )
         << QString::fromUtf8( schema )
         << QStringLiteral( "--max-turns" )
         << QStringLiteral( "1" );

    args << splitArgs( settings.extraArgs );

    return args;
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
                                        .arg( aiProviderName( provider_ ) ) );
                 }
                 else if ( exitStatus != QProcess::NormalExit || exitCode != 0 ) {
                     Q_EMIT failed( provider_, tr( "%1 exited with code %2." )
                                                   .arg( aiProviderName( provider_ ) )
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
                      .arg( aiProviderName( provider_ ), process_.errorString() ) );
        }
    } );
    connect( &timeout_, &QTimer::timeout, this, [ this ] {
        fail( tr( "%1 request timed out after %2 seconds." )
                  .arg( aiProviderName( provider_ ) )
                  .arg( DefaultTimeoutMs / 1000 ) );
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
    return availability( provider,
                         Configuration::get().aiProviderSettings( aiProviderName( provider ) ) );
}

QString AiCliBackend::availability( Provider provider, const AiProviderSettings& settings )
{
    const QString executable = resolveExecutable( provider, settings );
    if ( executable.isEmpty() ) {
        return tr( "%1 CLI was not found. Set the executable path in the provider settings." )
            .arg( aiProviderName( provider ) );
    }

#if !defined( Q_OS_WIN )
    if ( settings.useSandbox && settings.apiKey.isEmpty() && bubblewrapPath().isEmpty() ) {
        return tr( "bubblewrap (bwrap) is required to sandbox %1 from local files. "
                   "Disable the sandbox in provider settings or install bwrap." )
            .arg( aiProviderName( provider ) );
    }

    if ( provider == Provider::CodeBuddy && settings.apiKey.isEmpty()
         && ( !QDir( providerAuthDirectory() ).exists()
              || !QDir( providerExtensionAuthDirectory() ).exists() ) ) {
        return tr( "CodeBuddy login data is unavailable. Run codebuddy /login in a terminal, "
                   "set an API key, or disable the sandbox in provider settings." );
    }

    if ( settings.useSandbox && settings.apiKey.isEmpty()
         && ( !QDir::homePath().startsWith( QStringLiteral( "/home/" ) )
              || !QFileInfo( QStringLiteral( "/usr" ) ).isDir()
              || !QFileInfo( QStringLiteral( "/etc/ssl" ) ).isDir()
              || !QFileInfo( QStringLiteral( "/etc/hosts" ) ).isFile()
              || QFileInfo( QStringLiteral( "/etc/resolv.conf" ) ).canonicalFilePath().isEmpty() ) ) {
        return tr( "This Linux runtime does not provide the paths required for CodeBuddy isolation. "
                   "Disable the sandbox in provider settings to run locally." );
    }
#else
    Q_UNUSED( provider )
#endif

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

    const auto settings = Configuration::get().aiProviderSettings( aiProviderName( provider ) );
    const QString executable = resolveExecutable( provider, settings );
    const QStringList providerArgs = providerArguments( provider, schema, settings );

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if ( !settings.apiKey.isEmpty() ) {
        if ( provider == Provider::CodeBuddy ) {
            env.insert( QStringLiteral( "CODEBUDDY_API_KEY" ), settings.apiKey );
        }
        else if ( provider == Provider::Codex ) {
            env.insert( QStringLiteral( "OPENAI_API_KEY" ), settings.apiKey );
        }
    }

    QString program = executable;
    QStringList args = providerArgs;

#if !defined( Q_OS_WIN )
    if ( settings.useSandbox && settings.apiKey.isEmpty() ) {
        const QString home = QDir::homePath();
        const QString userState = home + QStringLiteral( "/.codebuddy/user-state.json" );
        const QString dnsConfig
            = QFileInfo( QStringLiteral( "/etc/resolv.conf" ) ).canonicalFilePath();
        const QString extensionAuth
            = QFileInfo( providerExtensionAuthDirectory() ).canonicalFilePath();
        const QString authDir = providerAuthDirectory();
        const QString execName = providerExecutableName( provider );
        const QString containerPath
            = QStringLiteral( "/opt/%1" ).arg( execName );

        QStringList sandboxArgs = {
            QStringLiteral( "--die-with-parent" ),
            QStringLiteral( "--unshare-pid" ),
            QStringLiteral( "--unshare-ipc" ),
            QStringLiteral( "--ro-bind" ),
            QStringLiteral( "/usr" ),
            QStringLiteral( "/usr" ),
            QStringLiteral( "--symlink" ),
            QStringLiteral( "usr/lib64" ),
            QStringLiteral( "/lib64" ),
            QStringLiteral( "--symlink" ),
            QStringLiteral( "usr/lib" ),
            QStringLiteral( "/lib" ),
            QStringLiteral( "--proc" ),
            QStringLiteral( "/proc" ),
            QStringLiteral( "--dev" ),
            QStringLiteral( "/dev" ),
            QStringLiteral( "--tmpfs" ),
            QStringLiteral( "/tmp" ),
            QStringLiteral( "--tmpfs" ),
            QStringLiteral( "/home" ),
            QStringLiteral( "--dir" ),
            home,
            QStringLiteral( "--dir" ),
            home + QStringLiteral( "/.codebuddy" ),
            QStringLiteral( "--ro-bind" ),
            authDir,
            authDir,
            QStringLiteral( "--dir" ),
            home + QStringLiteral( "/.local" ),
            QStringLiteral( "--dir" ),
            home + QStringLiteral( "/.local/share" ),
            QStringLiteral( "--dir" ),
            home + QStringLiteral( "/.local/share/CodeBuddyExtension" ),
            QStringLiteral( "--dir" ),
            home + QStringLiteral( "/.local/share/CodeBuddyExtension/Data" ),
            QStringLiteral( "--dir" ),
            home + QStringLiteral( "/.local/share/CodeBuddyExtension/Data/Public" ),
            QStringLiteral( "--ro-bind" ),
            extensionAuth,
            extensionAuth,
            QStringLiteral( "--dir" ),
            QStringLiteral( "/opt" ),
            QStringLiteral( "--ro-bind" ),
            executable,
            containerPath,
            QStringLiteral( "--dir" ),
            QStringLiteral( "/etc" ),
            QStringLiteral( "--ro-bind" ),
            QStringLiteral( "/etc/ssl" ),
            QStringLiteral( "/etc/ssl" ),
            QStringLiteral( "--ro-bind" ),
            QStringLiteral( "/etc/hosts" ),
            QStringLiteral( "/etc/hosts" ),
            QStringLiteral( "--ro-bind" ),
            dnsConfig,
            QStringLiteral( "/etc/resolv.conf" ),
            QStringLiteral( "--clearenv" ),
            QStringLiteral( "--setenv" ),
            QStringLiteral( "HOME" ),
            home,
            QStringLiteral( "--setenv" ),
            QStringLiteral( "PATH" ),
            QStringLiteral( "/usr/bin:/bin" ),
            QStringLiteral( "--setenv" ),
            QStringLiteral( "LANG" ),
            QStringLiteral( "C.UTF-8" ),
            QStringLiteral( "--chdir" ),
            QStringLiteral( "/tmp" ),
            QStringLiteral( "--" ),
            containerPath,
        };
        sandboxArgs << providerArgs;

        if ( provider == Provider::CodeBuddy && QFileInfo( userState ).isFile() ) {
            const int insertAt
                = sandboxArgs.indexOf( QStringLiteral( "--dir" ),
                                       sandboxArgs.indexOf( authDir ) );
            if ( insertAt >= 0 ) {
                sandboxArgs.insert( insertAt, QStringLiteral( "--ro-bind" ) );
                sandboxArgs.insert( insertAt + 1, userState );
                sandboxArgs.insert( insertAt + 2, userState );
            }
        }

        program = bubblewrapPath();
        args = std::move( sandboxArgs );
    }
#else
    Q_UNUSED( provider )
#endif

    provider_ = provider;
    prompt_ = input;
    stdout_.clear();
    stderr_.clear();
    active_ = true;
    process_.setProgram( program );
    process_.setArguments( args );
    process_.setProcessEnvironment( env );
    process_.setWorkingDirectory( QDir::homePath() );
    process_.start();
    timeout_.start( settings.timeoutMs > 0 ? settings.timeoutMs : DefaultTimeoutMs );
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
                  .arg( aiProviderName( provider_ ) )
                  .arg( MaxStdoutBytes / 1024 ) );
    }
}

void AiCliBackend::readStderr()
{
    stderr_ += process_.readAllStandardError();
    if ( stderr_.size() > MaxStderrBytes ) {
        fail( tr( "%1 diagnostic output exceeded the %2 KiB limit." )
                  .arg( aiProviderName( provider_ ) )
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

QString aiProviderName( AiCliBackend::Provider provider )
{
    return provider == AiCliBackend::Provider::Codex ? QStringLiteral( "Codex" )
                                                       : QStringLiteral( "CodeBuddy" );
}
