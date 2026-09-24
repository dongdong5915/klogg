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

#ifndef AIPROVIDERCONFIGDIALOG_H
#define AIPROVIDERCONFIGDIALOG_H

#include <QDialog>

#include "aiclibackend.h"

class QCheckBox;
class QLineEdit;
class QLabel;
class QSpinBox;

class AiProviderConfigDialog : public QDialog {
    Q_OBJECT

  public:
    explicit AiProviderConfigDialog( AiCliBackend::Provider provider, QWidget* parent = nullptr );

  private Q_SLOTS:
    void checkAvailability();
    void browseExecutable();
    void resetDefaults();

  private:
    void accept() override;

    AiCliBackend::Provider provider_;
    QLineEdit* pathEdit_;
    QLineEdit* apiKeyEdit_;
    QLineEdit* extraArgsEdit_;
    QSpinBox* timeoutSpin_;
    QCheckBox* sandboxCheck_;
    QLabel* statusLabel_;
};

#endif // AIPROVIDERCONFIGDIALOG_H
