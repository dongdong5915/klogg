#ifndef AIPANEL_H
#define AIPANEL_H

#include <QHash>
#include <QSet>
#include <QWidget>

#include "aiclibackend.h"
#include "crawlerwidget.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
class QEvent;

class AiPanel : public QWidget {
    Q_OBJECT

  public:
    explicit AiPanel( QWidget* parent = nullptr );
    void setContext( QString fileName, QString contextText );
    AiContextScope contextScope() const;
    void setStatus( const QString& message );
    void activateTab( QObject* tab, QString fileName );
    void forgetTab( QObject* tab );
    void cancelRequest();

  Q_SIGNALS:
    void contextRequested();
    void filterRequested( QString pattern );
    void highlightRequested( QString pattern );
    void undoFilterRequested();
    void undoHighlightRequested();
    void evidenceActivated( qulonglong lineNumber, QString expectedText );

  protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

  private:
    struct EvidenceTarget {
        qulonglong lineNumber = 0;
        QString expectedText;
    };

    struct TabState {
        QString conversationHtml;
        QString inputText;
        QString statusText;
        QString filterPattern;
        QStringList highlightPatterns;
        QHash<qulonglong, EvidenceTarget> evidenceTargets;
        bool undoFilterEnabled = false;
        bool undoHighlightsEnabled = false;
    };

    void submitQuestion();
    void startRequest();
    void stopRequest();
    void setRunning( bool running );
    void applyFilter();
    void applyHighlights();
    void undoFilter();
    void undoHighlights();
    bool confirmSend();
    void requestFinished( AiCliBackend::Provider provider, const QString& output );
    void requestFailed( AiCliBackend::Provider provider, const QString& message );
    void updateAvailability();

    AiCliBackend* backend_;
    QComboBox* providerBox_;
    QComboBox* scopeBox_;
    QCheckBox* previewBeforeSend_;
    QLabel* fileLabel_;
    QLabel* contextLabel_;
    QTextBrowser* conversation_;
    QPlainTextEdit* input_;
    QLabel* status_;
    QPushButton* stopButton_;
    QPushButton* applyFilterButton_;
    QPushButton* undoFilterButton_;
    QPushButton* applyHighlightsButton_;
    QPushButton* undoHighlightsButton_;
    QString fileName_;
    QString contextText_;
    QString pendingQuestion_;
    QString filterPattern_;
    QStringList highlightPatterns_;
    QSet<qulonglong> allowedEvidence_;
    QHash<qulonglong, QString> evidenceText_;
    QHash<qulonglong, EvidenceTarget> evidenceTargets_;
    QHash<QObject*, TabState> tabStates_;
    QObject* activeTab_ = nullptr;
    qulonglong nextEvidenceId_ = 0;
    bool requestPending_ = false;
};

#endif
