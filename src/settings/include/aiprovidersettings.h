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

#ifndef KLOGG_AI_PROVIDER_SETTINGS_H
#define KLOGG_AI_PROVIDER_SETTINGS_H

#include <QSettings>
#include <QString>

struct AiProviderSettings {
    QString executablePath;
    QString apiKey;
    bool useSandbox = true;
    int timeoutMs = 120000;
    QString extraArgs;

    void save( QSettings& settings, const QString& providerKey ) const;
    void restore( QSettings& settings, const QString& providerKey );

    bool operator==( const AiProviderSettings& other ) const = default;
};

#endif // KLOGG_AI_PROVIDER_SETTINGS_H
