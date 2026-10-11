#pragma once
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <functional>

class Backend;
class QQmlEngine;
class Theme;

// Presents the video process, as Omasnap presents captures. The editor is drawn over the
// dimmed live desktop in a full-screen layer on the focused monitor, or in a floating window
// (W toggles; the choice is remembered). After a recording, a card in the corner offers Edit
// and Export without taking the keyboard. Every surface hides while a countdown or recording
// runs. Each surface's content is a QML item (qml/Editor.qml, qml/Card.qml) that reaches this
// object as the `stage` context property.
class VideoStage : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool windowed READ windowed NOTIFY windowedChanged)
    Q_PROPERTY(QString card READ card NOTIFY cardChanged)
  public:
    VideoStage(QQmlEngine &engine, Backend &backend, Theme &theme);
    ~VideoStage() override;
    // Compiles the editor and card. False, with the errors logged, when the QML is broken.
    bool load();
    // Offers an earlier session's unfinished edit on the card, when there is one.
    void offerRecovery();
    bool windowed() const { return m_windowed; }
    // What the card shows: "recording", "recovery", or empty when it is closed.
    QString card() const { return m_card; }
    QQuickWindow *editorWindow() const { return editorHost; }
    QQuickWindow *cardWindow() const { return cardHost; }
    QQuickItem *editor() const { return editorItem; }

    Q_INVOKABLE void openEditor();
    // Switches between the overlay and a floating window, keeping the session and the
    // editor's own state.
    Q_INVOKABLE void setWindowed(bool windowed);
    // Closes the card. A recording's session ends with it unless the editor is open; the
    // recording itself stays in Videos.
    Q_INVOKABLE void closeCard();
    // Choosers, handed off as Omasnap's Save As: the overlay hides and releases the keyboard
    // while an independent floating dialog is open, and returns once it has gone.
    Q_INVOKABLE void chooseBackground();
    Q_INVOKABLE void chooseColor();
    Q_INVOKABLE void chooseExport(bool gif, int width, int fps, int quality);
    // Exports straight into Videos with these settings: the card's Export.
    Q_INVOKABLE void exportNow(bool gif, int width, int fps, int quality);
    // Opens the folder holding `path` in the file manager.
    Q_INVOKABLE void reveal(const QString &path);
  signals:
    void windowedChanged();
    void cardChanged();
    // Nothing is open and nothing is running, so the process may exit.
    void finished();

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private slots:
    void noteRecordingFinished();

  private:
    QQmlEngine &engine;
    Backend &backend;
    QQmlComponent editorComponent, cardComponent;
    QPointer<QQuickWindow> editorHost, cardHost;
    QPointer<QQuickItem> editorItem, cardItem;
    QString lastPhase, m_card;
    // `choosing`: a chooser has the screen, so the overlay stays hidden.
    bool m_windowed = false, editorOpen = false, recordingDone = false, choosing = false;

    void phaseChanged();
    void decideSession();
    void showCard(const QString &kind);
    void dropCard();
    void closeEditor();
    void hostEditor();
    void destroyHost(QPointer<QQuickWindow> &host);
    void showSurfaces();
    void hideSurfaces();
    void finishIfIdle();
    bool idle() const;
    bool recordingPhase() const;
    void handOff(const std::function<void()> &choose);
};
