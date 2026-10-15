#pragma once

class QDockWidget;
class QScrollArea;

namespace Ui {
// Wraps an existing adjustment scroll area in a compact navigation toolbar
// (classification combo plus expand-all / collapse-all buttons) without
// touching the scroll area contents, its identity, or its signals.
// Call once, immediately after dock->setWidget(scroll).
class AdjustmentNavigation;
void installAdjustmentNavigation(QDockWidget *dock, QScrollArea *scroll);
}