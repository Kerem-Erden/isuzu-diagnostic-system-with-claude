#pragma once
/**
 * @file style.hpp
 * @brief Application style sheet and banner styles (palette taken from the reference WPF app:
 *        Isuzu red #A5001E, text #30343B, muted #68707C, borders #D5D8DE, background #F8F9FA).
 */

#include <QString>

namespace style {

QString applicationStyleSheet();
/// Banner for SIMULATION: unmistakable, never resembles the vehicle banner.
QString simulationBanner();
/// Banner for VEHICLE: solid red, with the "not vehicle-validated" text set by the caller.
QString vehicleBanner();
QString disconnectedBanner();

}  // namespace style
