/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <qcolor.h>

#include "configuration.h"
#include "log.h"
#include "styles.h"

QStringList StyleManager::availableStyles()
{
    QStringList styles;
#ifdef Q_OS_WIN
    styles << VistaKey;
    styles << WindowsKey;
    styles << FusionKey;
#else
    styles << QStyleFactory::keys();
#endif

    auto removedStyles = std::remove_if( styles.begin(), styles.end(), []( const QString& style ) {
        return style.startsWith( Gtk2Key, Qt::CaseInsensitive )
               || style.startsWith( Bb10Key, Qt::CaseInsensitive );
    } );

    styles.erase( removedStyles, styles.end() );

    styles << DarkStyleKey;

#ifndef Q_OS_MACOS
    styles << DarkWindowsStyleKey;
#endif

    std::sort( styles.begin(), styles.end(), []( const auto& lhs, const auto& rhs ) {
        return lhs.compare( rhs, Qt::CaseInsensitive ) < 0;
    } );

    return styles;
}

QString StyleManager::defaultPlatformStyle()
{
#if defined( Q_OS_WIN )
    return VistaKey;
#elif defined( Q_OS_MACOS )
    return MacintoshKey;
#else
    return FusionKey;
#endif
}

QStringList StyleManager::availableColorThemes()
{
    return { DefaultThemeKey, DarkModernThemeKey, LightModernThemeKey, DarkPlusThemeKey,
             DarkHighContrastThemeKey };
}

QPalette StyleManager::themePalette( const QString& theme )
{
    QPalette palette = qApp->palette();
    QColor windowColor( "#1f1f1f" );
    QColor baseColor( "#181818" );
    QColor alternateColor( "#252526" );
    QColor textColor( "#cccccc" );
    QColor accentColor( "#0078d4" );
    QColor highlightColor( "#264f78" );
    QColor highlightedTextColor( "#ffffff" );
    QColor disabledTextColor( "#9b9b9b" );

    if ( theme == LightModernThemeKey ) {
        windowColor = QColor( "#f3f3f3" );
        baseColor = QColor( "#ffffff" );
        alternateColor = QColor( "#e8e8e8" );
        textColor = QColor( "#1f1f1f" );
        accentColor = QColor( "#005fb8" );
        highlightColor = QColor( "#add6ff" );
        highlightedTextColor = QColor( "#1f1f1f" );
        disabledTextColor = QColor( "#606060" );
    }
    else if ( theme == DarkPlusThemeKey ) {
        windowColor = QColor( "#252526" );
        baseColor = QColor( "#1e1e1e" );
        alternateColor = QColor( "#2d2d30" );
        textColor = QColor( "#d4d4d4" );
        accentColor = QColor( "#007acc" );
        highlightColor = QColor( "#264f78" );
    }
    else if ( theme == DarkHighContrastThemeKey ) {
        windowColor = QColor( "#000000" );
        baseColor = QColor( "#000000" );
        alternateColor = QColor( "#1a1a1a" );
        textColor = QColor( "#ffffff" );
        accentColor = QColor( "#ffff00" );
        highlightColor = QColor( "#ffffff" );
        highlightedTextColor = QColor( "#000000" );
        disabledTextColor = QColor( "#ffffff" );
    }
    else if ( theme == DefaultThemeKey ) {
        return qApp->palette();
    }

    palette.setColor( QPalette::Window, windowColor );
    palette.setColor( QPalette::WindowText, textColor );
    palette.setColor( QPalette::Base, baseColor );
    palette.setColor( QPalette::AlternateBase, alternateColor );
    palette.setColor( QPalette::ToolTipBase, windowColor );
    palette.setColor( QPalette::ToolTipText, textColor );
    palette.setColor( QPalette::Text, textColor );
    palette.setColor( QPalette::Button, windowColor );
    palette.setColor( QPalette::ButtonText, textColor );
    palette.setColor( QPalette::Link, accentColor );
    palette.setColor( QPalette::Highlight, highlightColor );
    palette.setColor( QPalette::HighlightedText, highlightedTextColor );
    palette.setColor( QPalette::Disabled, QPalette::Text, disabledTextColor );
    palette.setColor( QPalette::Disabled, QPalette::WindowText, disabledTextColor );
    palette.setColor( QPalette::Disabled, QPalette::ButtonText, disabledTextColor );
    palette.setColor( QPalette::Disabled, QPalette::Highlight, alternateColor );
    return palette;
}

bool StyleManager::isDarkTheme( const QString& theme, const QString& style )
{
    if ( theme == DefaultThemeKey ) {
        if ( style == DarkStyleKey || style == DarkWindowsStyleKey ) {
            return true;
        }

        return qApp->palette().color( QPalette::Window ).lightness() < 128;
    }

    return theme == DarkModernThemeKey || theme == DarkPlusThemeKey
           || theme == DarkHighContrastThemeKey;
}

void StyleManager::applyStyle( const QString& style )
{
    applyStyle( style, DefaultThemeKey );
}

void StyleManager::applyStyle( const QString& style, const QString& theme )
{
    LOG_INFO << "Setting style to " << style << " and color theme to " << theme;

    const bool legacyDarkStyle = style == DarkStyleKey || style == DarkWindowsStyleKey;
    QString widgetStyle = style;
    if ( style == DarkStyleKey ) {
        widgetStyle = FusionKey;
    }
    else if ( style == DarkWindowsStyleKey ) {
        widgetStyle = WindowsKey;
    }

    qApp->setStyle( widgetStyle );
    qApp->setStyleSheet( "" );

    if ( theme != DefaultThemeKey ) {
        qApp->setPalette( themePalette( theme ) );
        return;
    }

    if ( !legacyDarkStyle ) {
        return;
    }

    const auto legacyPalette = Configuration::get().darkPalette();
    QPalette darkPalette{};
    darkPalette.setColor( QPalette::Window, QColor( legacyPalette.at( "Window" ) ) );
    darkPalette.setColor( QPalette::WindowText, QColor( legacyPalette.at( "WindowText" ) ) );
    darkPalette.setColor( QPalette::Base, QColor( legacyPalette.at( "Base" ) ) );
    darkPalette.setColor( QPalette::AlternateBase,
                          QColor( legacyPalette.at( "AlternateBase" ) ) );
    darkPalette.setColor( QPalette::ToolTipBase, QColor( legacyPalette.at( "ToolTipBase" ) ) );
    darkPalette.setColor( QPalette::ToolTipText, QColor( legacyPalette.at( "ToolTipText" ) ) );
    darkPalette.setColor( QPalette::Text, QColor( legacyPalette.at( "Text" ) ) );
    darkPalette.setColor( QPalette::Button, QColor( legacyPalette.at( "Button" ) ) );
    darkPalette.setColor( QPalette::ButtonText, QColor( legacyPalette.at( "ButtonText" ) ) );
    darkPalette.setColor( QPalette::Link, QColor( legacyPalette.at( "Link" ) ) );
    darkPalette.setColor( QPalette::Highlight, QColor( legacyPalette.at( "Highlight" ) ) );
    darkPalette.setColor( QPalette::HighlightedText,
                          QColor( legacyPalette.at( "HighlightedText" ) ) );
    darkPalette.setColor( QPalette::Active, QPalette::Button,
                          QColor( legacyPalette.at( "ActiveButton" ) ) );
    darkPalette.setColor( QPalette::Disabled, QPalette::ButtonText,
                          QColor( legacyPalette.at( "DisabledButtonText" ) ) );
    darkPalette.setColor( QPalette::Disabled, QPalette::WindowText,
                          QColor( legacyPalette.at( "DisabledWindowText" ) ) );
    darkPalette.setColor( QPalette::Disabled, QPalette::Text,
                          QColor( legacyPalette.at( "DisabledText" ) ) );
    darkPalette.setColor( QPalette::Disabled, QPalette::Light,
                          QColor( legacyPalette.at( "DisabledLight" ) ) );
    qApp->setPalette( darkPalette );
}
