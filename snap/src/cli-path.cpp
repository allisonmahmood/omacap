#include <QCommandLineParser>
#include <QCommandLineOption>
/** @fileoverview Resolves local image targets accepted by the command line. */
#include "cli-path.hpp"

#include <QCommandLineParser>
#include <QCommandLineOption>

#include <QFileInfo>
#include <QUrl>

QString resolveLocalImagePath(const QString &target) {
  if (target.isEmpty())
    return {};

  const QUrl url(target);
  QString path;
  if (url.isLocalFile())
    path = url.toLocalFile();
  else if (!url.scheme().isEmpty() &&
           target.startsWith(url.scheme() + QStringLiteral("://"),
                             Qt::CaseInsensitive))
    return {};
  else
    path = target;

  const QFileInfo file(path);
  return file.isFile() ? file.absoluteFilePath() : QString{};
}

void configureCaptureCommandLine(QCommandLineParser &parser, bool beforeQt) {
  // QApplication consumes these before the normal parse. Recognize them in
  // the early shell-role parse too, without exposing or applying them here.
  if (beforeQt) {
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    for (const char *name : {"platform", "platformpluginpath", "platformtheme",
                             "plugin", "qmljsdebugger", "qwindowgeometry",
                             "qwindowicon", "qwindowtitle", "session",
                             "style", "stylesheet"}) {
      QCommandLineOption option(QString::fromLatin1(name), QString(),
                                 QStringLiteral("value"));
      option.setFlags(QCommandLineOption::HiddenFromHelp);
      parser.addOption(option);
    }
    for (const char *name : {"reverse", "widgetcount"}) {
      QCommandLineOption option(QString::fromLatin1(name));
      option.setFlags(QCommandLineOption::HiddenFromHelp);
      parser.addOption(option);
    }
  }
  // `omacap --record` and videos never reach this parser (the top-level
  // main() sends them to the video process), but the help describes them.
  parser.setApplicationDescription(QStringLiteral(
      "Screenshots and screen recordings for Omarchy on Hyprland.\n"
      "\n"
      "  omacap               Pick an area, a window or the monitor to "
      "screenshot.\n"
      "  omacap MODE          Start the picker in a mode: smart, region, "
      "windows,\n"
      "                       fullscreen or scroll.\n"
      "  omacap --record      Pick an area to record. Run it again to stop "
      "the\n"
      "                       recording.\n"
      "  omacap IMAGE         Annotate an image in the screenshot editor.\n"
      "  omacap VIDEO         Edit a video (.mp4, .mkv, .webm, .mov, .m4v, "
      ".avi, .gif).\n"
      "\n"
      "A screenshot is saved, copied to the clipboard and previewed for 10 "
      "seconds;\n"
      "pin the preview (Ctrl+P) to keep it, or Edit it. Press E in the picker, "
      "or use\n"
      "--editor, to edit before output. V switches the picker between "
      "screenshot and\n"
      "record.\n"
      "\n"
      "Running omacap while the picker is open closes it, so one key opens "
      "and closes\n"
      "it. --copy and --save close it the same way. An image, --file or "
      "--clipboard\n"
      "replaces it with the editor on that image.\n"
      "\n"
      "Exit codes: 0 success, including closing an open picker; 1 capture, "
      "image or\n"
      "lock failure; 2 usage error."));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption fullscreenOption(
      QStringLiteral("capture-fullscreen"),
      QStringLiteral("Start with the entire focused monitor selected."));
  const QCommandLineOption windowOption(
      {QStringLiteral("capture-window"), QStringLiteral("capture-windows")},
      QStringLiteral("Start in window selection mode."));
  const QCommandLineOption regionOption(
      QStringLiteral("capture-region"),
      QStringLiteral("Start in freeform region selection mode."));
  parser.addOption(fullscreenOption);
  parser.addOption(windowOption);
  parser.addOption(regionOption);
  const QCommandLineOption copyOption(
      QStringLiteral("copy"),
      QStringLiteral("Copy the capture without opening an editor or pin."));
  const QCommandLineOption saveOption(
      QStringLiteral("save"),
      QStringLiteral("Save the capture without opening an editor or pin."));
  parser.addOption(copyOption);
  parser.addOption(saveOption);
  const QCommandLineOption fileOption(
      QStringLiteral("file"),
      QStringLiteral("Open an existing image file in the annotation editor "
                     "instead of capturing the screen."),
      QStringLiteral("path"));
  parser.addOption(fileOption);
  const QCommandLineOption clipboardOption(
      QStringLiteral("clipboard"),
      QStringLiteral("Open the current clipboard image in the annotation "
                     "editor instead of capturing the screen."));
  parser.addOption(clipboardOption);
  const QCommandLineOption pinOption(
      QStringLiteral("pin"),
      QStringLiteral("Show an image as a floating window pinned on every workspace."),
      QStringLiteral("path"));
  parser.addOption(pinOption);
  QCommandLineOption previewOption(
      QStringLiteral("preview"), QString(), QStringLiteral("path"));
  previewOption.setFlags(QCommandLineOption::HiddenFromHelp);
  parser.addOption(previewOption);
  const QCommandLineOption editorOption(
      QStringLiteral("editor"),
      QStringLiteral("Edit before output, using overlay (fullscreen) or "
                     "window (a normal compositor window). Also configurable "
                     "as [editor] mode in omacap.conf; W switches a live "
                     "editor between the two."),
      QStringLiteral("mode"));
  parser.addOption(editorOption);
  QCommandLineOption handoffMonitor(QStringLiteral("handoff-monitor"), QString(),
                                     QStringLiteral("name"));
  handoffMonitor.setFlags(QCommandLineOption::HiddenFromHelp);
  parser.addOption(handoffMonitor);
  QCommandLineOption handoffToken(QStringLiteral("handoff-token"), QString(),
                                  QStringLiteral("token"));
  handoffToken.setFlags(QCommandLineOption::HiddenFromHelp);
  parser.addOption(handoffToken);
  // `omacap --record` opens the picker in record mode through this.
  QCommandLineOption recordPicker(QStringLiteral("record-picker"));
  recordPicker.setFlags(QCommandLineOption::HiddenFromHelp);
  parser.addOption(recordPicker);
  QCommandLineOption pinDocument(QStringLiteral("pin-document"), QString(),
                                 QStringLiteral("path"));
  pinDocument.setFlags(QCommandLineOption::HiddenFromHelp);
  parser.addOption(pinDocument);
  const QCommandLineOption scrollOption(
      QStringLiteral("scroll"),
      QStringLiteral("Capture a scrolling region and stitch it into one tall "
                     "image, then copy it and show a timed preview."));
  parser.addOption(scrollOption);
  parser.addPositionalArgument(
      QStringLiteral("target"),
      QStringLiteral("Capture mode (smart, region, windows, fullscreen, scroll), an "
                     "image to annotate, or a video to edit."),
      QStringLiteral("[target]"));
}

bool windowedEditorRequested(const QCommandLineParser &parser, bool defaultWindow) {
  const QString mode = parser.value(QStringLiteral("editor")).trimmed().toLower();
  const bool window = mode.isEmpty() ? defaultWindow : mode == QStringLiteral("window");
  if (!window || parser.isSet(QStringLiteral("pin")) ||
      parser.isSet(QStringLiteral("preview")))
    return false;
  if (!parser.value(QStringLiteral("file")).isEmpty() ||
      parser.isSet(QStringLiteral("clipboard")))
    return true;
  const QStringList positional = parser.positionalArguments();
  return positional.size() == 1 && !resolveLocalImagePath(positional.first()).isEmpty();
}
