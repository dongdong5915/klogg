#ifndef AIRESPONSE_H
#define AIRESPONSE_H

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

struct AiResponse {
    QString intent;
    QString summary;
    QString filterPattern;
    QStringList highlightPatterns;
    QList<qulonglong> evidenceLines;
    QString uncertainty;
};

class AiResponseParser {
  public:
    static QByteArray schema();
    static bool parse( const QByteArray& output, const QSet<qulonglong>& allowedEvidence,
                       AiResponse* response, QString* error );
};

#endif
