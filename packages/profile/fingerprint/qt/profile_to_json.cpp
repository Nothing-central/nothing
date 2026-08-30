#include "profile_to_json.h"
#include <QJsonArray>

static QString ToQString(const std::string& s) { return QString::fromStdString(s); }

QJsonObject NavigatorProfileToJson(const NavigatorProfile& p) {
    QJsonObject o;
    o["userAgent"] = ToQString(p.user_agent);
    o["platform"] = ToQString(p.platform);
    o["oscpu"] = ToQString(p.oscpu);
    o["appVersion"] = ToQString(p.app_version);
    o["appName"] = ToQString(p.app_name);
    o["appCodeName"] = ToQString(p.app_code_name);
    o["product"] = ToQString(p.product);
    o["productSub"] = ToQString(p.product_sub);
    o["vendor"] = ToQString(p.vendor);
    o["vendorSub"] = ToQString(p.vendor_sub);
    o["buildId"] = ToQString(p.build_id);
    o["hardwareConcurrency"] = static_cast<int>(p.hardware_concurrency);
    o["deviceMemory"] = p.device_memory ? QJsonValue(*p.device_memory) : QJsonValue();
    o["maxTouchPoints"] = static_cast<int>(p.max_touch_points);

    QJsonArray langs;
    for (const auto& l : p.languages) langs.append(ToQString(l));
    o["languages"] = langs;

    o["intlLocale"] = ToQString(p.intl_locale);
    o["acceptLanguageHeader"] = ToQString(p.accept_language_header);
    o["timezone"] = ToQString(p.timezone);
    o["timezoneOffsetMinutes"] = p.timezone_offset_minutes;
    o["pdfViewerEnabled"] = p.pdf_viewer_enabled;
    o["webdriver"] = p.webdriver;
    o["cookieEnabled"] = p.cookie_enabled;
    o["online"] = p.online;
    o["doNotTrack"] = ToQString(p.do_not_track);
    o["globalPrivacyControl"] = p.global_privacy_control;

    if (p.user_agent_data) {
        QJsonObject uaData;
        QJsonArray brands;
        for (const auto& b : p.user_agent_data->brands) {
            QJsonObject brand;
            brand["brand"] = ToQString(b.first);
            brand["version"] = ToQString(b.second);
            brands.append(brand);
        }
        uaData["brands"] = brands;
        uaData["mobile"] = p.user_agent_data->mobile;
        uaData["platform"] = ToQString(p.user_agent_data->platform);
        o["userAgentData"] = uaData;
    } else {
        o["userAgentData"] = QJsonValue();
    }

    return o;
}

QJsonObject ScreenProfileToJson(const ScreenProfile& s) {
    QJsonObject o;
    o["width"] = s.width;
    o["height"] = s.height;
    o["availLeft"] = s.avail_left;
    o["availTop"] = s.avail_top;
    o["colorDepth"] = s.color_depth;
    o["pixelDepth"] = s.pixel_depth;
    o["devicePixelRatio"] = s.device_pixel_ratio;
    o["screenX"] = s.screen_x;
    o["screenY"] = s.screen_y;
    o["colorGamut"] = ToQString(s.color_gamut);
    o["orientationType"] = ToQString(s.orientation_type);
    o["orientationAngle"] = s.orientation_angle;
    return o;
}

QJsonObject WebGLProfileToJson(const WebGLProfile& g) {
    QJsonObject o;
    o["vendor"] = ToQString(g.vendor);
    o["renderer"] = ToQString(g.renderer);
    o["version"] = ToQString(g.version);
    o["shadingLanguageVersion"] = ToQString(g.shading_language_version);
    o["maxTextureSize"] = static_cast<int>(g.max_texture_size);
    o["maxCubeMapTextureSize"] = static_cast<int>(g.max_cube_map_texture_size);
    o["maxRenderbufferSize"] = static_cast<int>(g.max_renderbuffer_size);
    o["maxVertexAttribs"] = static_cast<int>(g.max_vertex_attribs);
    o["maxVaryingVectors"] = static_cast<int>(g.max_varying_vectors);
    o["maxViewportDims"] = static_cast<int>(g.max_viewport_dims);

    QJsonArray ext;
    for (const auto& e : g.supported_extensions) ext.append(ToQString(e));
    o["extensions"] = ext;
    return o;
}

QJsonObject AudioProfileToJson(const AudioProfile& a) {
    QJsonObject o;
    o["sampleRate"] = a.sample_rate;
    o["maxChannelCount"] = static_cast<int>(a.max_channel_count);
    o["baseLatency"] = a.base_latency;
    o["outputLatency"] = a.output_latency;
    return o;
}

QString KeyToHex(const Key32& k) {
    static const char* hex = "0123456789abcdef";
    QString out;
    out.reserve(64);
    for (uint8_t b : k) {
        out += hex[b >> 4];
        out += hex[b & 0xF];
    }
    return out;
}