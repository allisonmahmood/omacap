/** @fileoverview The picker's record mode: the request it builds and hands
 *  off, and that it never captures. */
#pragma once

#include <QString>

class QApplication;

[[nodiscard]] bool runRecordSmoke(QApplication &application, QString &error);
