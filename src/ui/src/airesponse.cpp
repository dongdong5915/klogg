#include "airesponse.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

bool hasKeys( const QJsonObject& object, const QStringList& expected )
{
    if ( object.size() != expected.size() ) {
        return false;
    }

    for ( const auto& key : expected ) {
        if ( !object.contains( key ) ) {
            return false;
        }
    }

    return true;
}

bool validString( const QJsonValue& value, int maxLength, bool allowEmpty = true )
{
    return value.isString() && value.toString().size() <= maxLength
           && ( allowEmpty || !value.toString().isEmpty() );
}

// Returns the structured payload and reports terminal failures through *error.
QJsonObject resultObject( const QByteArray& output, QString* error )
{
    const auto document = QJsonDocument::fromJson( output );
    if ( !document.isArray() && !document.isObject() ) {
        *error = QStringLiteral( "AI output is not valid JSON" );
        return {};
    }

    QJsonObject wrapper{};
    if ( document.isArray() ) {
        // --output-format json returns the whole transcript. Only the terminal entry of
        // type "result" carries the payload, the rest is conversation history.
        const auto entries = document.array();
        for ( auto index = entries.size(); index > 0; ) {
            --index;
            const auto entry = entries.at( index ).toObject();
            if ( entry.value( QStringLiteral( "type" ) ) == QLatin1String( "result" ) ) {
                wrapper = entry;
                break;
            }
        }

        if ( wrapper.isEmpty() ) {
            *error = QStringLiteral( "AI output has no result entry" );
            return {};
        }

        if ( wrapper.value( QStringLiteral( "is_error" ) ).toBool() ) {
            *error = QStringLiteral( "AI provider reported an error" );
            return {};
        }
    }
    else {
        wrapper = document.object();
    }

    if ( wrapper.contains( QStringLiteral( "version" ) ) ) {
        return wrapper;
    }
    if ( wrapper.value( QStringLiteral( "structured_output" ) ).isObject() ) {
        return wrapper.value( QStringLiteral( "structured_output" ) ).toObject();
    }
    if ( wrapper.value( QStringLiteral( "result" ) ).isString() ) {
        const auto inner = QJsonDocument::fromJson(
            wrapper.value( QStringLiteral( "result" ) ).toString().toUtf8() );
        if ( inner.isObject() ) {
            return inner.object();
        }
    }

    return {};
}

} // namespace

QByteArray AiResponseParser::schema()
{
    return R"json({"type":"object","additionalProperties":false,"required":["version","intent","summary","filter","highlights","evidence_ids","uncertainty"],"properties":{"version":{"type":"integer","const":1},"intent":{"type":"string","enum":["analyze","filter","highlight","analyze_filter","analyze_highlight","needs_clarification"]},"summary":{"type":"string","maxLength":8000},"filter":{"type":"object","additionalProperties":false,"required":["pattern"],"properties":{"pattern":{"type":"string","maxLength":256}}},"highlights":{"type":"array","maxItems":5,"items":{"type":"string","minLength":1,"maxLength":256}},"evidence_ids":{"type":"array","maxItems":80,"items":{"type":"string","pattern":"^L[1-9][0-9]*$"}},"uncertainty":{"type":"string","maxLength":2000}}})json";
}

bool AiResponseParser::parse( const QByteArray& output, const QSet<qulonglong>& allowedEvidence,
                              AiResponse* response, QString* error )
{
    if ( response == nullptr || error == nullptr ) {
        return false;
    }
    if ( output.size() > 128 * 1024 ) {
        *error = QStringLiteral( "AI response exceeds size limit" );
        return false;
    }

    QString extractionError{};
    const auto result = resultObject( output, &extractionError );
    if ( result.isEmpty() ) {
        *error = extractionError.isEmpty() ? QStringLiteral( "AI response does not match schema" )
                                           : extractionError;
        return false;
    }

    const QStringList rootKeys{ QStringLiteral( "version" ), QStringLiteral( "intent" ),
                                QStringLiteral( "summary" ), QStringLiteral( "filter" ),
                                QStringLiteral( "highlights" ), QStringLiteral( "evidence_ids" ),
                                QStringLiteral( "uncertainty" ) };
    if ( !hasKeys( result, rootKeys ) || !result.value( "version" ).isDouble()
         || result.value( "version" ).toInt( -1 ) != 1
         || !validString( result.value( "intent" ), 32, false )
         || !validString( result.value( "summary" ), 8000 )
         || !validString( result.value( "uncertainty" ), 2000 )
         || !result.value( "filter" ).isObject() || !result.value( "highlights" ).isArray()
         || !result.value( "evidence_ids" ).isArray() ) {
        *error = QStringLiteral( "AI response does not match schema" );
        return false;
    }

    const QString intent = result.value( "intent" ).toString();
    const QStringList intents{ QStringLiteral( "analyze" ), QStringLiteral( "filter" ),
                               QStringLiteral( "highlight" ), QStringLiteral( "analyze_filter" ),
                               QStringLiteral( "analyze_highlight" ),
                               QStringLiteral( "needs_clarification" ) };
    if ( !intents.contains( intent ) ) {
        *error = QStringLiteral( "Unsupported AI intent" );
        return false;
    }

    const auto filter = result.value( "filter" ).toObject();
    if ( !hasKeys( filter, { QStringLiteral( "pattern" ) } )
         || !validString( filter.value( "pattern" ), 256 ) ) {
        *error = QStringLiteral( "Invalid filter suggestion" );
        return false;
    }

    const auto highlights = result.value( "highlights" ).toArray();
    if ( highlights.size() > 5 ) {
        *error = QStringLiteral( "Too many highlight suggestions" );
        return false;
    }
    QStringList patterns{};
    for ( const auto& value : highlights ) {
        if ( !validString( value, 256, false ) || value.toString().contains( QLatin1Char( '\n' ) ) ) {
            *error = QStringLiteral( "Invalid highlight suggestion" );
            return false;
        }
        patterns.append( value.toString() );
    }

    const QString filterPattern = filter.value( "pattern" ).toString();
    if ( filterPattern.contains( QLatin1Char( '\n' ) )
         || filterPattern.contains( QLatin1Char( '\r' ) ) ) {
        *error = QStringLiteral( "AI action does not match intent" );
        return false;
    }

    // Filter and highlight suggestions may be combined: the panel previews each one and the
    // user applies them separately. Only needs_clarification must stay free of actions.
    if ( intent == QLatin1String( "needs_clarification" )
         && ( !filterPattern.isEmpty() || !patterns.isEmpty() ) ) {
        *error = QStringLiteral( "AI action does not match intent" );
        return false;
    }

    const auto evidence = result.value( "evidence_ids" ).toArray();
    if ( evidence.size() > 80 ) {
        *error = QStringLiteral( "Too many evidence references" );
        return false;
    }
    const QRegularExpression evidencePattern{ QStringLiteral( "^L[1-9][0-9]*$" ) };
    QList<qulonglong> evidenceLines{};
    for ( const auto& value : evidence ) {
        if ( !validString( value, 24, false )
             || !evidencePattern.match( value.toString() ).hasMatch() ) {
            *error = QStringLiteral( "Invalid evidence reference" );
            return false;
        }
        bool validNumber = false;
        const qulonglong line = value.toString().mid( 1 ).toULongLong( &validNumber );
        if ( !validNumber || !allowedEvidence.contains( line ) ) {
            *error = QStringLiteral( "Evidence is outside the supplied snapshot" );
            return false;
        }
        evidenceLines.append( line );
    }

    response->intent = intent;
    response->summary = result.value( "summary" ).toString();
    response->filterPattern = filterPattern;
    response->highlightPatterns = patterns;
    response->evidenceLines = evidenceLines;
    response->uncertainty = result.value( "uncertainty" ).toString();
    error->clear();
    return true;
}
