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

#include "aiprovidersettings.h"

void AiProviderSettings::save( QSettings& settings, const QString& providerKey ) const
{
    settings.beginGroup( QStringLiteral( "aiProviders/%1" ).arg( providerKey ) );
    settings.setValue( QStringLiteral( "executablePath" ), executablePath );
    settings.setValue( QStringLiteral( "apiKey" ), apiKey );
    settings.setValue( QStringLiteral( "useSandbox" ), useSandbox );
    settings.setValue( QStringLiteral( "timeoutMs" ), timeoutMs );
    settings.setValue( QStringLiteral( "extraArgs" ), extraArgs );
    settings.endGroup();
}

void AiProviderSettings::restore( QSettings& settings, const QString& providerKey )
{
    settings.beginGroup( QStringLiteral( "aiProviders/%1" ).arg( providerKey ) );
    executablePath = settings.value( QStringLiteral( "executablePath" ), executablePath ).toString();
    apiKey = settings.value( QStringLiteral( "apiKey" ), apiKey ).toString();
    useSandbox = settings.value( QStringLiteral( "useSandbox" ), useSandbox ).toBool();
    timeoutMs = settings.value( QStringLiteral( "timeoutMs" ), timeoutMs ).toInt();
    extraArgs = settings.value( QStringLiteral( "extraArgs" ), extraArgs ).toString();
    settings.endGroup();
}
