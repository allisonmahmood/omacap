/** @fileoverview OmaCap's screenshot look: layout maths, persistence through
 *  the operation log, and how the card is painted. */
#pragma once

#include <QString>

class QApplication;

[[nodiscard]] bool runLookSmoke(QApplication &application, QString &error);
