#include "screen.h"
#include "common.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

#include <windows.h>
#include <objidl.h>
// gdiplus.h uses min and max unqualified, and NOMINMAX (updater.pro) takes the macros away.
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <shlwapi.h>

namespace up {

namespace {

constexpr UINT k_MsgPhase = WM_APP + 2;
constexpr UINT k_MsgClose = WM_APP + 3;
constexpr UINT_PTR k_FadeTimer = 1;
constexpr double k_Pi = 3.14159265358979323846;

// ---- look.json ------------------------------------------------------------------------------

bool num(const QJsonValue& v, double lo, double hi, double& out)
{
    if (!v.isDouble()) return false;
    const double d = v.toDouble();
    if (!std::isfinite(d) || d < lo || d > hi) return false;
    out = d;
    return true;
}

bool nums(const QJsonValue& v, int n, double lo, double hi, double* out)
{
    const QJsonArray a = v.toArray();
    if (a.size() != n) return false;
    for (int i = 0; i < n; ++i) {
        if (!num(a.at(i), lo, hi, out[i])) return false;
    }
    return true;
}

// "#rrggbb" or Qt's "#aarrggbb".
bool colour(const QJsonValue& v, quint32& out)
{
    static const QRegularExpression re(QStringLiteral("^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    const QString s = v.toString();
    if (!re.match(s).hasMatch()) return false;
    bool ok = false;
    const quint32 value = s.mid(1).toUInt(&ok, 16);
    if (!ok) return false;
    out = s.size() == 7 ? (0xff000000u | value) : value;
    return true;
}

quint32 tint(quint32 base, quint32 over, double a)
{
    auto ch = [&](int shift) {
        const double b = (base >> shift) & 0xff, o = (over >> shift) & 0xff;
        return quint32(std::lround(b + (o - b) * a)) << shift;
    };
    return 0xff000000u | ch(16) | ch(8) | ch(0);
}

// ---- drawing --------------------------------------------------------------------------------

struct Fonts {
    QByteArray data[3];
    Gdiplus::PrivateFontCollection* collection = nullptr;
    Gdiplus::FontFamily* medium = nullptr;      // DM Sans Medium: the status line
    Gdiplus::FontFamily* semibold = nullptr;    // DM Sans SemiBold: the wordmark
    Gdiplus::FontFamily* mono = nullptr;        // JetBrains Mono: percentage, versions
    Gdiplus::FontFamily* all = nullptr;
    int count = 0;
};

struct State {
    Look look;
    QString from, to;
    ScreenPhase phase = ScreenPhase::Closing;
    int progress = 0;           // 0..1000
    int alpha = 255;
    int width = 0, height = 0;
    HDC bgDc = nullptr;
    HBITMAP bgBitmap = nullptr;
    HGDIOBJ bgOld = nullptr;
    Fonts fonts;
    QByteArray iconData;
    HANDLE ready = nullptr;
    HWND hwnd = nullptr;
};

State* s_State = nullptr;

Gdiplus::Color argb(quint32 c)
{
    return Gdiplus::Color(BYTE(c >> 24), BYTE(c >> 16), BYTE(c >> 8), BYTE(c));
}

QByteArray resource(const char* name)
{
    QFile f(QString::fromLatin1(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void loadFonts(Fonts& f)
{
    const char* names[3] = { ":/DMSans-Medium.ttf", ":/DMSans-SemiBold.ttf", ":/JetBrainsMono-Regular.ttf" };
    f.collection = new Gdiplus::PrivateFontCollection();
    for (int i = 0; i < 3; ++i) {
        f.data[i] = resource(names[i]);
        if (!f.data[i].isEmpty()) f.collection->AddMemoryFont(f.data[i].constData(), INT(f.data[i].size()));
    }
    f.count = f.collection->GetFamilyCount();
    if (f.count <= 0) return;
    f.all = new Gdiplus::FontFamily[f.count];
    int found = 0;
    f.collection->GetFamilies(f.count, f.all, &found);
    f.count = found;
    for (int i = 0; i < found; ++i) {
        WCHAR name[LF_FACESIZE] = {};
        f.all[i].GetFamilyName(name);
        const QString n = QString::fromWCharArray(name);
        log(QStringLiteral("screen: font family \"%1\"").arg(n));
        if (n.contains(QLatin1String("JetBrains"), Qt::CaseInsensitive)) f.mono = &f.all[i];
        else if (n.contains(QLatin1String("SemiBold"), Qt::CaseInsensitive)) f.semibold = &f.all[i];
        else if (n.contains(QLatin1String("Medium"), Qt::CaseInsensitive)) f.medium = &f.all[i];
        else if (n.contains(QLatin1String("DM Sans"), Qt::CaseInsensitive)) {
            // One family holding several faces: take it for whatever is still missing.
            if (!f.medium) f.medium = &f.all[i];
            if (!f.semibold) f.semibold = &f.all[i];
        }
    }
    if (!f.medium && found > 0) f.medium = &f.all[0];
    if (!f.semibold) f.semibold = f.medium;
    if (!f.mono) f.mono = f.medium;
}

INT styleFor(const Gdiplus::FontFamily* family)
{
    if (family->IsStyleAvailable(Gdiplus::FontStyleRegular)) return Gdiplus::FontStyleRegular;
    return Gdiplus::FontStyleBold;
}

enum Align { Left, Centre, Right };

double textWidth(Gdiplus::Graphics& g, const Gdiplus::FontFamily* family, double px, const QString& text)
{
    if (!family) return 0;
    Gdiplus::Font font(family, Gdiplus::REAL(px), styleFor(family), Gdiplus::UnitPixel);
    Gdiplus::RectF box;
    const std::wstring w = wide(text);
    g.MeasureString(w.c_str(), INT(w.size()), &font, Gdiplus::PointF(0, 0),
                    Gdiplus::StringFormat::GenericTypographic(), &box);
    return box.Width;
}

void drawText(Gdiplus::Graphics& g, const Gdiplus::FontFamily* family, double px, const QString& text,
              double x, double baseline, quint32 colour, Align align)
{
    if (!family || text.isEmpty() || px <= 0) return;
    const INT style = styleFor(family);
    Gdiplus::Font font(family, Gdiplus::REAL(px), style, Gdiplus::UnitPixel);
    const double ascent = px * family->GetCellAscent(style) / qMax<UINT16>(1, family->GetEmHeight(style));
    if (align != Left) {
        const double w = textWidth(g, family, px, text);
        x -= align == Centre ? w / 2 : w;
    }
    Gdiplus::SolidBrush brush(argb(colour));
    const std::wstring w = wide(text);
    g.DrawString(w.c_str(), INT(w.size()), &font, Gdiplus::PointF(Gdiplus::REAL(x), Gdiplus::REAL(baseline - ascent)),
                 Gdiplus::StringFormat::GenericTypographic(), &brush);
}

void fillRounded(Gdiplus::Graphics& g, double x, double y, double w, double h, quint32 colour)
{
    if (w <= 0 || h <= 0) return;
    Gdiplus::SolidBrush brush(argb(colour));
    const double r = qMin(h, w) / 2;
    if (r < 0.5) {
        g.FillRectangle(&brush, Gdiplus::REAL(x), Gdiplus::REAL(y), Gdiplus::REAL(w), Gdiplus::REAL(h));
        return;
    }
    Gdiplus::GraphicsPath path;
    const Gdiplus::REAL d = Gdiplus::REAL(r * 2);
    path.AddArc(Gdiplus::REAL(x), Gdiplus::REAL(y), d, d, 90, 180);
    path.AddArc(Gdiplus::REAL(x + w - r * 2), Gdiplus::REAL(y), d, d, 270, 180);
    path.CloseFigure();
    g.FillPath(&brush, &path);
}

/*
 * The floor: AmbientBackground's three stops, dithered as DitheredGradient dithers it (the
 * same ramp on 8 bits bands badly in the dark end), then AmbientWaves' five layers, stopped
 * where StreamLight's stopped — the same formula, the same constants.
 */
void drawFloor(BYTE* bits, int w, int h, const Look& look)
{
    static const double bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    auto chan = [](quint32 c, int shift) { return double((c >> shift) & 0xff); };
    for (int y = 0; y < h; ++y) {
        const double p = h > 1 ? double(y) / (h - 1) : 0;
        quint32 a, b;
        double k;
        if (p <= 0.7) { a = look.top; b = look.mid; k = p / 0.7; }
        else { a = look.mid; b = look.foot; k = (p - 0.7) / 0.3; }
        double rgb[3];
        for (int c = 0; c < 3; ++c) {
            const int shift = 16 - 8 * c;
            rgb[c] = chan(a, shift) + (chan(b, shift) - chan(a, shift)) * k;
        }
        BYTE* row = bits + size_t(y) * size_t(w) * 4;
        for (int x = 0; x < w; ++x) {
            const double d = (bayer[(x & 3) + ((y & 3) << 2)] + 0.5) / 16.0 - 0.5;
            row[x * 4 + 2] = BYTE(qBound(0.0, std::floor(rgb[0] + d + 0.5), 255.0));
            row[x * 4 + 1] = BYTE(qBound(0.0, std::floor(rgb[1] + d + 0.5), 255.0));
            row[x * 4 + 0] = BYTE(qBound(0.0, std::floor(rgb[2] + d + 0.5), 255.0));
            row[x * 4 + 3] = 255;
        }
    }
}

void drawWaves(Gdiplus::Graphics& g, int w, int h, const Look& look)
{
    const double s = look.dpr, u = look.u, t = look.waveTime;
    const double W = w / s, H = h / s;
    const BYTE ir = BYTE(look.ink >> 16), ig = BYTE(look.ink >> 8), ib = BYTE(look.ink);
    for (int i = 0; i < 5; ++i) {
        const double depth = i / 4.0;
        const double level = H * (0.25 + 0.53 * depth);
        const double amp = (12 + 22 * depth) * u;
        const double wavelength = (720 + 780 * depth) * u;
        const double travel = 34000 - 18000 * depth;
        const double swellAmp = (4 + 6 * depth) * u;
        const double swellPeriod = 11000 + 2300 * i;
        const double phase = i * 1.7;
        const double fillAlpha = 0.022 + 0.022 * depth;
        const double crestAlpha = 0.07 + 0.13 * depth;
        const double below = amp + swellAmp + 8 * u;
        const double width = W + wavelength;
        const double height = H - level + amp + below;
        const double cycle = t / travel + i * 0.37;
        const double x0 = -wavelength + wavelength * (cycle - std::floor(cycle));
        const double y0 = level - amp + swellAmp * std::sin(t / swellPeriod * 2 * k_Pi + phase);

        const double step = qMax(6.0, 10 * u);
        const int n = int(std::ceil(width / step));
        QVector<Gdiplus::PointF> crest;
        crest.reserve(n + 4);
        for (int k = 0; k <= n; ++k) {
            const double px = k * step;
            const double a = px / wavelength * 2 * k_Pi;
            const double py = amp * (1 - 0.72 * std::sin(a + phase) - 0.28 * std::sin(2 * a + phase * 2.3));
            crest.append(Gdiplus::PointF(Gdiplus::REAL((x0 + px) * s), Gdiplus::REAL((y0 + py) * s)));
        }
        QVector<Gdiplus::PointF> body = crest;
        body.append(Gdiplus::PointF(Gdiplus::REAL((x0 + width) * s), Gdiplus::REAL((y0 + height) * s)));
        body.append(Gdiplus::PointF(Gdiplus::REAL(x0 * s), Gdiplus::REAL((y0 + height) * s)));

        Gdiplus::SolidBrush fill(Gdiplus::Color(BYTE(std::lround(fillAlpha * 255)), ir, ig, ib));
        g.FillPolygon(&fill, body.constData(), INT(body.size()));
        Gdiplus::Pen pen(Gdiplus::Color(BYTE(std::lround(crestAlpha * 255)), ir, ig, ib),
                         Gdiplus::REAL(qMax(1.0, 1.5 * u) * s));
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawLines(&pen, crest.constData(), INT(crest.size()));
    }
}

void buildBackground(State& st)
{
    const Look& look = st.look;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = st.width;
    bi.bmiHeader.biHeight = -st.height;         // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    st.bgDc = CreateCompatibleDC(screen);
    st.bgBitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!st.bgBitmap || !bits) return;
    st.bgOld = SelectObject(st.bgDc, st.bgBitmap);

    drawFloor(static_cast<BYTE*>(bits), st.width, st.height, look);

    Gdiplus::Graphics g(st.bgDc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    if (look.waves) drawWaves(g, st.width, st.height, look);

    // The icon, from this program's own resources.
    if (!st.iconData.isEmpty() && look.icon[2] > 0) {
        IStream* stream = SHCreateMemStream(reinterpret_cast<const BYTE*>(st.iconData.constData()),
                                            UINT(st.iconData.size()));
        if (stream) {
            Gdiplus::Bitmap* icon = Gdiplus::Bitmap::FromStream(stream);
            if (icon && icon->GetLastStatus() == Gdiplus::Ok) {
                g.DrawImage(icon, Gdiplus::REAL(look.icon[0]), Gdiplus::REAL(look.icon[1]),
                            Gdiplus::REAL(look.icon[2]), Gdiplus::REAL(look.icon[3]));
            }
            delete icon;
            stream->Release();
        }
    }

    // STREAMLIGHT, a letter at a time where StreamLight placed each one.
    static const QString word = QStringLiteral("STREAMLIGHT");
    if (look.wordX[0] < 0) {
        const double spacing = look.wordPx * 0.32;
        double total = -spacing;
        for (QChar c : word) total += textWidth(g, st.fonts.semibold, look.wordPx, QString(c)) + spacing;
        double x = (st.width - total) / 2;
        for (QChar c : word) {
            drawText(g, st.fonts.semibold, look.wordPx, QString(c), x, look.wordBaseline, look.text, Left);
            x += textWidth(g, st.fonts.semibold, look.wordPx, QString(c)) + spacing;
        }
    } else {
        for (int i = 0; i < word.size(); ++i) {
            drawText(g, st.fonts.semibold, look.wordPx, QString(word.at(i)), look.wordX[i], look.wordBaseline,
                     look.text, Left);
        }
    }

    if (!st.from.isEmpty() && !st.to.isEmpty()) {
        drawText(g, st.fonts.mono, look.versionPx, st.from + QStringLiteral("  →  ") + st.to,
                 look.versionCx, look.versionBaseline, look.text3, Centre);
    }
}

void paint(State& st, HDC target)
{
    HDC frameDc = CreateCompatibleDC(target);
    HBITMAP frame = CreateCompatibleBitmap(target, st.width, st.height);
    HGDIOBJ old = SelectObject(frameDc, frame);
    BitBlt(frameDc, 0, 0, st.width, st.height, st.bgDc, 0, 0, SRCCOPY);
    {
        const Look& look = st.look;
        Gdiplus::Graphics g(frameDc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

        QString status, percent;
        switch (st.phase) {
        case ScreenPhase::Closing:    status = QStringLiteral("Closing StreamLight"); break;
        case ScreenPhase::Installing: status = QStringLiteral("Installing");
                                      percent = QStringLiteral("%1%").arg(st.progress / 10); break;
        case ScreenPhase::Starting:   status = QStringLiteral("Starting StreamLight");
                                      percent = QStringLiteral("100%"); break;
        case ScreenPhase::Failed:     status = QStringLiteral("The update did not install"); break;
        }
        drawText(g, st.fonts.medium, look.statusPx, status, look.statusX, look.statusBaseline, look.text, Left);
        drawText(g, st.fonts.mono, look.percentPx, percent, look.percentRight, look.percentBaseline, look.text2, Right);
        fillRounded(g, look.bar[0], look.bar[1], look.bar[2], look.bar[3], look.line);
        const double done = st.phase == ScreenPhase::Starting ? 1.0
                          : st.phase == ScreenPhase::Installing ? st.progress / 1000.0 : 0.0;
        fillRounded(g, look.bar[0], look.bar[1], look.bar[2] * done, look.bar[3], look.ink);
    }
    BitBlt(target, 0, 0, st.width, st.height, frameDc, 0, 0, SRCCOPY);
    SelectObject(frameDc, old);
    DeleteObject(frame);
    DeleteDC(frameDc);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    State* st = s_State;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (st && st->bgDc) paint(*st, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(nullptr);
        return TRUE;
    case WM_CLOSE:
        return 0;           // Alt+F4 included: this goes away when the update is done
    case k_MsgSetupProgress:
        if (st && lParam > 0) {
            const qint64 ratio = qint64(wParam) * 1000 / qint64(lParam);
            const int p = int(qBound(qint64(0), ratio, qint64(1000)));
            if (p > st->progress) {
                st->progress = p;
                if (st->phase == ScreenPhase::Closing) st->phase = ScreenPhase::Installing;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case k_MsgPhase:
        if (st) {
            st->phase = ScreenPhase(wParam);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case k_MsgClose:
        SetTimer(hwnd, k_FadeTimer, 15, nullptr);
        return 0;
    case WM_TIMER:
        if (wParam == k_FadeTimer && st) {
            st->alpha -= 255 * 15 / 300;
            if (st->alpha <= 0) {
                KillTimer(hwnd, k_FadeTimer);
                DestroyWindow(hwnd);
            } else {
                SetLayeredWindowAttributes(hwnd, 0, BYTE(st->alpha), LWA_ALPHA);
            }
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

DWORD WINAPI screenThread(void*)
{
    State& st = *s_State;
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
    loadFonts(st.fonts);
    st.iconData = resource(":/icon.png");

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = k_ScreenClass;
    wc.hCursor = nullptr;
    RegisterClassExW(&wc);

    const RECT& r = st.look.rect;
    st.width = r.right - r.left;
    st.height = r.bottom - r.top;
    buildBackground(st);
    st.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, k_ScreenClass, L"StreamLight",
                              WS_POPUP, r.left, r.top, st.width, st.height, nullptr, nullptr, wc.hInstance, nullptr);
    if (st.hwnd) {
        SetLayeredWindowAttributes(st.hwnd, 0, 255, LWA_ALPHA);
        ShowWindow(st.hwnd, SW_SHOWNA);
        UpdateWindow(st.hwnd);
    }
    SetEvent(st.ready);

    MSG msg;
    while (st.hwnd && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (st.bgDc) {
        SelectObject(st.bgDc, st.bgOld);
        DeleteObject(st.bgBitmap);
        DeleteDC(st.bgDc);
    }
    delete[] st.fonts.all;
    delete st.fonts.collection;
    Gdiplus::GdiplusShutdown(token);
    return 0;
}

} // namespace

bool parseLook(const QByteArray& json, Look& out)
{
    if (json.size() > 16 * 1024) return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("v")).toInt() != 1) return false;

    Look l;
    double rect[4];
    if (!nums(o.value(QStringLiteral("rect")), 4, -65536, 65536, rect) || rect[2] < 64 || rect[3] < 64
            || rect[2] > 16384 || rect[3] > 16384) {
        return false;
    }
    l.rect = { LONG(rect[0]), LONG(rect[1]), LONG(rect[0] + rect[2]), LONG(rect[1] + rect[3]) };
    if (!MonitorFromRect(&l.rect, MONITOR_DEFAULTTONULL)) return false;     // on no screen at all

    const double maxXY = 16384;
    if (!num(o.value(QStringLiteral("dpr")), 0.5, 8, l.dpr)
            || !num(o.value(QStringLiteral("u")), 0.1, 20, l.u)
            || !num(o.value(QStringLiteral("t")), 0, 1e13, l.waveTime)) {
        return false;
    }
    l.waves = o.value(QStringLiteral("waves")).toBool(true);

    const QJsonObject c = o.value(QStringLiteral("colors")).toObject();
    if (!colour(c.value(QStringLiteral("top")), l.top) || !colour(c.value(QStringLiteral("mid")), l.mid)
            || !colour(c.value(QStringLiteral("foot")), l.foot) || !colour(c.value(QStringLiteral("ink")), l.ink)
            || !colour(c.value(QStringLiteral("text")), l.text) || !colour(c.value(QStringLiteral("text2")), l.text2)
            || !colour(c.value(QStringLiteral("text3")), l.text3) || !colour(c.value(QStringLiteral("line")), l.line)) {
        return false;
    }

    const QJsonObject word = o.value(QStringLiteral("word")).toObject();
    const QJsonObject status = o.value(QStringLiteral("status")).toObject();
    const QJsonObject percent = o.value(QStringLiteral("percent")).toObject();
    const QJsonObject version = o.value(QStringLiteral("version")).toObject();
    if (!nums(o.value(QStringLiteral("icon")), 4, -maxXY, maxXY, l.icon)
            || !nums(o.value(QStringLiteral("bar")), 4, -maxXY, maxXY, l.bar)
            || !num(word.value(QStringLiteral("px")), 1, 1000, l.wordPx)
            || !num(word.value(QStringLiteral("baseline")), -maxXY, maxXY, l.wordBaseline)
            || !nums(word.value(QStringLiteral("x")), 11, -maxXY, maxXY, l.wordX)
            || !num(status.value(QStringLiteral("px")), 1, 1000, l.statusPx)
            || !num(status.value(QStringLiteral("x")), -maxXY, maxXY, l.statusX)
            || !num(status.value(QStringLiteral("baseline")), -maxXY, maxXY, l.statusBaseline)
            || !num(percent.value(QStringLiteral("px")), 1, 1000, l.percentPx)
            || !num(percent.value(QStringLiteral("right")), -maxXY, maxXY, l.percentRight)
            || !num(percent.value(QStringLiteral("baseline")), -maxXY, maxXY, l.percentBaseline)
            || !num(version.value(QStringLiteral("px")), 1, 1000, l.versionPx)
            || !num(version.value(QStringLiteral("cx")), -maxXY, maxXY, l.versionCx)
            || !num(version.value(QStringLiteral("baseline")), -maxXY, maxXY, l.versionBaseline)) {
        return false;
    }
    if (l.icon[2] < 0 || l.icon[3] < 0 || l.bar[2] < 0 || l.bar[3] < 0) return false;
    for (double& x : l.wordX) x = qMax(0.0, x);     // a negative first letter means "lay it out yourself"
    out = l;
    return true;
}

Look defaultLook()
{
    Look l;
    HMONITOR mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    l.rect = mi.rcMonitor;
    l.dpr = qMax(1.0, GetDpiForSystem() / 96.0);
    const double W = (l.rect.right - l.rect.left) / l.dpr, H = (l.rect.bottom - l.rect.top) / l.dpr;
    l.u = W / 1330.0;
    l.waveTime = 30000;
    // Dark, default cyan accent, as theme.h.
    const quint32 floor = 0xff151515, accent = 0xff00d3f2;
    l.top = floor;
    l.mid = tint(floor, accent, 0.07);
    l.foot = tint(floor, accent, 0.20);
    l.ink = accent;
    const double s = l.dpr, u = l.u;
    const double iconSize = 150 * u;
    l.icon[0] = (W - iconSize) / 2 * s;
    l.icon[1] = 0.20 * H * s;
    l.icon[2] = l.icon[3] = iconSize * s;
    l.wordPx = std::round(36 * u) * s;
    l.wordBaseline = (0.20 * H + iconSize + 24 * u + 36 * u * 0.95) * s;
    l.wordX[0] = -1;                                   // centred by drawBackground
    const double blockW = 480 * u, x0 = (W - blockW) / 2;
    l.statusPx = 21 * u * s;
    l.statusX = x0 * s;
    l.statusBaseline = l.wordBaseline + (64 + 21) * u * s;
    l.percentPx = 17 * u * s;
    l.percentRight = (x0 + blockW) * s;
    l.percentBaseline = l.statusBaseline;
    l.bar[0] = x0 * s;
    l.bar[1] = l.statusBaseline + 12 * u * s;
    l.bar[2] = blockW * s;
    l.bar[3] = qMax(3.0, 5 * u) * s;
    l.versionPx = 15 * u * s;
    l.versionCx = W / 2 * s;
    l.versionBaseline = l.bar[1] + l.bar[3] + (12 + 15) * u * s;
    return l;
}

bool Screen::start(const Look& look, const QString& fromVersion, const QString& toVersion)
{
    if (s_State) return m_Hwnd != nullptr;
    s_State = new State();
    s_State->look = look;
    s_State->from = fromVersion;
    s_State->to = toVersion;
    s_State->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_Thread = CreateThread(nullptr, 0, screenThread, nullptr, 0, nullptr);
    if (!m_Thread) return false;
    WaitForSingleObject(s_State->ready, 10000);
    m_Hwnd = s_State->hwnd;
    log(QStringLiteral("screen: %1").arg(m_Hwnd ? QStringLiteral("shown") : QStringLiteral("failed")));
    return m_Hwnd != nullptr;
}

void Screen::setPhase(ScreenPhase phase)
{
    if (m_Hwnd) PostMessageW(m_Hwnd, k_MsgPhase, WPARAM(phase), 0);
}

void Screen::close()
{
    if (m_Hwnd) PostMessageW(m_Hwnd, k_MsgClose, 0, 0);
    if (m_Thread) {
        WaitForSingleObject(m_Thread, 5000);
        CloseHandle(m_Thread);
        m_Thread = nullptr;
    }
    m_Hwnd = nullptr;
}

Screen::~Screen()
{
    close();
}

} // namespace up
