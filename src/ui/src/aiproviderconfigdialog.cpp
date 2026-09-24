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

#include "aiproviderconfigdialog.h"

#include "configuration.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

AiProviderConfigDialog::AiProviderConfigDialog( AiCliBackend::Provider provider, QWidget* parent )
    : QDialog( parent )
    , provider_( provider )
{
    setWindowTitle( tr( "%1 Provider Settings" ).arg( aiProviderName( provider ) ) );

    const AiProviderSettings current
        = Configuration::get().aiProviderSettings( aiProviderName( provider ) );

    pathEdit_ = new QLineEdit( current.executablePath, this );
    pathEdit_->setPlaceholderText( tr( "Auto-detect from PATH" ) );

    auto* browseButton = new QPushButton( tr( "Browse" ), this );
    auto* checkButton = new QPushButton( tr( "Check" ), this );
    auto* resetButton = new QPushButton( tr( "Reset" ), this );

    auto* pathLayout = new QHBoxLayout{};
    pathLayout->addWidget( pathEdit_, 1 );
    pathLayout->addWidget( browseButton );
    pathLayout->addWidget( checkButton );
    pathLayout->addWidget( resetButton );

    statusLabel_ = new QLabel( this );
    statusLabel_->setWordWrap( true );

    apiKeyEdit_ = new QLineEdit( current.apiKey, this );
    apiKeyEdit_->setEchoMode( QLineEdit::Password );
    apiKeyEdit_->setPlaceholderText( tr( "Optional API key" ) );

    extraArgsEdit_ = new QLineEdit( current.extraArgs, this );
    extraArgsEdit_->setPlaceholderText( tr( "Extra command-line arguments" ) );

    timeoutSpin_ = new QSpinBox( this );
    timeoutSpin_->setRange( 10, 600 );
    timeoutSpin_->setSuffix( tr( "s" ) );
    timeoutSpin_->setValue( qMax( current.timeoutMs / 1000, 10 ) );

    sandboxCheck_ = new QCheckBox( tr( "Run inside bubblewrap sandbox (Linux only)" ), this );
    sandboxCheck_->setChecked( current.useSandbox );

#if defined( Q_OS_WIN )
    sandboxCheck_->setEnabled( false );
    sandboxCheck_->setChecked( false );
#endif

    auto* advancedLayout = new QFormLayout{};
    advancedLayout->addRow( tr( "API key:" ), apiKeyEdit_ );
    advancedLayout->addRow( tr( "Request timeout:" ), timeoutSpin_ );
    advancedLayout->addRow( tr( "Extra arguments:" ), extraArgsEdit_ );
    advancedLayout->addRow( sandboxCheck_ );

    auto* advancedGroup = new QGroupBox( tr( "Authentication and configuration (optional)" ), this );
    advancedGroup->setLayout( advancedLayout );

    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );

    auto* layout = new QVBoxLayout( this );
    layout->addWidget( new QLabel( tr( "Executable path:" ), this ) );
    layout->addLayout( pathLayout );
    layout->addWidget( statusLabel_ );
    layout->addWidget( advancedGroup );
    layout->addStretch();
    layout->addWidget( buttons );

    connect( browseButton, &QPushButton::clicked, this,
             &AiProviderConfigDialog::browseExecutable );
    connect( checkButton, &QPushButton::clicked, this,
             &AiProviderConfigDialog::checkAvailability );
    connect( resetButton, &QPushButton::clicked, this, &AiProviderConfigDialog::resetDefaults );
    connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );

    checkAvailability();
}

void AiProviderConfigDialog::checkAvailability()
{
    AiProviderSettings settings;
    settings.executablePath = pathEdit_->text().trimmed();
    settings.apiKey = apiKeyEdit_->text();
    settings.extraArgs = extraArgsEdit_->text().trimmed();
    settings.timeoutMs = timeoutSpin_->value() * 1000;
    settings.useSandbox = sandboxCheck_->isChecked();

    const QString message = AiCliBackend::availability( provider_, settings );
    if ( message.isEmpty() ) {
        statusLabel_->setText(
            tr( "Detected: %1 is ready to use." ).arg( aiProviderName( provider_ ) ) );
        statusLabel_->setStyleSheet( QStringLiteral( "QLabel { color: green; }" ) );
    }
    else {
        statusLabel_->setText( message );
        statusLabel_->setStyleSheet( QStringLiteral( "QLabel { color: red; }" ) );
    }
}

void AiProviderConfigDialog::browseExecutable()
{
#if defined( Q_OS_WIN )
    const QString filter = tr( "Executable files (*.exe);;All files (*.*)" );
#else
    const QString filter = tr( "All files (*)" );
#endif
    const QString path
        = QFileDialog::getOpenFileName( this,
                                        tr( "Select %1 executable" ).arg( aiProviderName( provider_ ) ),
                                        pathEdit_->text().trimmed(), filter );
    if ( !path.isEmpty() ) {
        pathEdit_->setText( path );
        checkAvailability();
    }
}

void AiProviderConfigDialog::resetDefaults()
{
    pathEdit_->clear();
    apiKeyEdit_->clear();
    extraArgsEdit_->clear();
    timeoutSpin_->setValue( 120 );
    sandboxCheck_->setChecked( true );
#if defined( Q_OS_WIN )
    sandboxCheck_->setChecked( false );
#endif
    checkAvailability();
}

void AiProviderConfigDialog::accept()
{
    AiProviderSettings settings;
    settings.executablePath = pathEdit_->text().trimmed();
    settings.apiKey = apiKeyEdit_->text();
    settings.extraArgs = extraArgsEdit_->text().trimmed();
    settings.timeoutMs = qMax( timeoutSpin_->value() * 1000, 10000 );
    settings.useSandbox = sandboxCheck_->isChecked();

    Configuration::get().setAiProviderSettings( aiProviderName( provider_ ), settings );
    Configuration::get().save();

    QDialog::accept();
}
