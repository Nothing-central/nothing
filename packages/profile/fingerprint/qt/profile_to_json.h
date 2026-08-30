#pragma once
#include "navigator_profile.h"
#include "screen_profile.h"
#include "webgl_profile.h"
#include "audio_profile.h"
#include "site_key.h"
#include <QJsonObject>
#include <QString>

// Thin adapters — no logic, just struct -> QJsonObject.
// All real values come from your existing include/ structs.
QJsonObject NavigatorProfileToJson(const NavigatorProfile& p);
QJsonObject ScreenProfileToJson(const ScreenProfile& s);
QJsonObject WebGLProfileToJson(const WebGLProfile& g);
QJsonObject AudioProfileToJson(const AudioProfile& a);
QString KeyToHex(const Key32& k);