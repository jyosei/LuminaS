#pragma once
#include <QString>
#include <functional>
// Background-thread phase receipts, not GUI calls or estimates of time left.
using SourceReadProgress=std::function<void(const QString &,int,int)>;
