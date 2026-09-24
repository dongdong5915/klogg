#include <catch2/catch.hpp>

#include "airesponse.h"

TEST_CASE( "AI response validates actions against the supplied lines", "[ui]" )
{
    const QSet<qulonglong> allowedLines{ 10, 11 };
    const QByteArray validResponse = R"json({"version":1,"intent":"analyze_filter","summary":"Retry at L10","filter":{"pattern":"retry"},"highlights":[],"evidence_ids":["L10"],"uncertainty":""})json";
    AiResponse response{};
    QString error{};

    REQUIRE( AiResponseParser::parse( validResponse, allowedLines, &response, &error ) );
    REQUIRE( response.filterPattern == QLatin1String( "retry" ) );
    REQUIRE( response.evidenceLines == QList<qulonglong>{ 10 } );

    QByteArray forgedEvidence = validResponse;
    forgedEvidence.replace( "L10\"", "L999\"" );
    REQUIRE_FALSE( AiResponseParser::parse( forgedEvidence, allowedLines, &response, &error ) );

    QByteArray unknownAction = validResponse;
    unknownAction.replace( "analyze_filter", "execute" );
    REQUIRE_FALSE( AiResponseParser::parse( unknownAction, allowedLines, &response, &error ) );
}

TEST_CASE( "AI response accepts the CLI transcript array", "[ui]" )
{
    // codebuddy --output-format json returns the whole transcript, so the payload is the
    // structured_output of the terminal entry of type "result".
    const QByteArray transcript = R"json([{"type":"message","role":"user","content":[]},{"type":"reasoning","content":"thinking"},{"type":"result","subtype":"success","is_error":false,"result":"free text","structured_output":{"version":1,"intent":"analyze_filter","summary":"Authentication failed with status=0x05.","filter":{"pattern":"auth failed status=0x05"},"highlights":["0x05"],"evidence_ids":["L10","L13"],"uncertainty":"Only the snapshot was analyzed."}}])json";

    const QSet<qulonglong> allowedLines{ 10, 11, 12, 13 };
    AiResponse response{};
    QString error{};

    REQUIRE( AiResponseParser::parse( transcript, allowedLines, &response, &error ) );
    REQUIRE( response.intent == QLatin1String( "analyze_filter" ) );
    REQUIRE( response.filterPattern == QLatin1String( "auth failed status=0x05" ) );
    REQUIRE( response.highlightPatterns == QStringList{ QStringLiteral( "0x05" ) } );
    REQUIRE( response.evidenceLines == QList<qulonglong>{ 10, 13 } );

    // Evidence is still checked against the snapshot when the payload is nested.
    QByteArray forged = transcript;
    forged.replace( "\"L13\"", "\"L99\"" );
    REQUIRE_FALSE( AiResponseParser::parse( forged, allowedLines, &response, &error ) );

    // A provider level error must not be mistaken for a payload.
    QByteArray failed = transcript;
    failed.replace( "\"is_error\":false", "\"is_error\":true" );
    REQUIRE_FALSE( AiResponseParser::parse( failed, allowedLines, &response, &error ) );

    // A transcript without a result entry carries no payload.
    const QByteArray noResult = R"json([{"type":"message","role":"user","content":[]}])json";
    REQUIRE_FALSE( AiResponseParser::parse( noResult, allowedLines, &response, &error ) );
}
