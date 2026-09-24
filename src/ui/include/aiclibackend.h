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

#ifndef AICLIBACKEND_H
#define AICLIBACKEND_H

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

#include "aiprovidersettings.h"

class AiCliBackend : public QObject {
    Q_OBJECT

public:
    enum class Provider { CodeBuddy, Codex };
    Q_ENUM( Provider )

    explicit AiCliBackend( QObject* parent = nullptr );
    ~AiCliBackend() override;

    static QString availability( Provider provider, const AiProviderSettings& settings );
    QString availability( Provider provider ) const;
    bool start( Provider provider, const QString& prompt, const QByteArray& schema );
    void cancel();

Q_SIGNALS:
    void finished( AiCliBackend::Provider provider, QString rawStdout );
    void failed( AiCliBackend::Provider provider, QString message );
    void cancelled();

private:
    void readStdout();
    void readStderr();
    void fail( QString message );

    QProcess process_;
    QTimer timeout_;
    QByteArray prompt_;
    QByteArray stdout_;
    QByteArray stderr_;
    Provider provider_ = Provider::CodeBuddy;
    bool active_ = false;
};

QString aiProviderName( AiCliBackend::Provider provider );

#endif // AICLIBACKEND_H
