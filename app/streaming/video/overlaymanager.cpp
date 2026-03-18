#include "overlaymanager.h"
#include "path.h"

using namespace Overlay;

OverlayManager::OverlayManager() :
    m_Renderer(nullptr),
    m_FontData(Path::readDataFile("RobotoMono.ttf"))
{
    memset(m_Overlays, 0, sizeof(m_Overlays));

    m_Overlays[OverlayType::OverlayDebug].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayDebug].fontSize = 20;
    m_Overlays[OverlayType::OverlayDebug].x = 0;
    m_Overlays[OverlayType::OverlayDebug].y = 0;

    m_Overlays[OverlayType::OverlayStatusUpdate].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayStatusUpdate].fontSize = 36;
    m_Overlays[OverlayType::OverlayStatusUpdate].x = 0;
    m_Overlays[OverlayType::OverlayStatusUpdate].y = 0;

    // While TTF will usually not be initialized here, it is valid for that not to
    // be the case, since Session destruction is deferred and could overlap with
    // the lifetime of a new Session object.
    //SDL_assert(TTF_WasInit() == 0);

    if (TTF_Init() != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_Init() failed: %s",
                    TTF_GetError());
        return;
    }
}

OverlayManager::~OverlayManager()
{
    for (int i = 0; i < OverlayType::OverlayMax; i++) {
        if (m_Overlays[i].surface != nullptr) {
            SDL_FreeSurface(m_Overlays[i].surface);
        }
        if (m_Overlays[i].font != nullptr) {
            TTF_CloseFont(m_Overlays[i].font);
        }
    }

    TTF_Quit();

    // For similar reasons to the comment in the constructor, this will usually,
    // but not always, deinitialize TTF. In the cases where Session objects overlap
    // in lifetime, there may be an additional reference on TTF for the new Session
    // that means it will not be cleaned up here.
    //SDL_assert(TTF_WasInit() == 0);
}

bool OverlayManager::isOverlayEnabled(OverlayType type)
{
    return m_Overlays[type].enabled;
}

char* OverlayManager::getOverlayText(OverlayType type)
{
    return m_Overlays[type].text;
}

void OverlayManager::updateOverlayText(OverlayType type, const char* text)
{
    strncpy(m_Overlays[type].text, text, sizeof(m_Overlays[0].text));
    m_Overlays[type].text[getOverlayMaxTextLength() - 1] = '\0';

    setOverlayTextUpdated(type);
}

int OverlayManager::getOverlayMaxTextLength()
{
    return sizeof(m_Overlays[0].text);
}

int OverlayManager::getOverlayFontSize(OverlayType type)
{
    return m_Overlays[type].fontSize;
}

SDL_Surface* OverlayManager::getUpdatedOverlaySurface(OverlayType type)
{
    // If a new surface is available, return it. If not, return nullptr.
    // Caller must free the surface on success.
    return (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);
}

void OverlayManager::setOverlayTextUpdated(OverlayType type)
{
    // Only update the overlay state if it's enabled. If it's not enabled,
    // the renderer has already been notified by setOverlayState().
    if (m_Overlays[type].enabled) {
        notifyOverlayUpdated(type);
    }
}

void OverlayManager::setOverlayState(OverlayType type, bool enabled)
{
    bool stateChanged = m_Overlays[type].enabled != enabled;

    m_Overlays[type].enabled = enabled;

    if (stateChanged) {
        if (!enabled) {
            // Set the text to empty string on disable
            m_Overlays[type].text[0] = 0;
        }

        notifyOverlayUpdated(type);
    }
}

SDL_Color OverlayManager::getOverlayColor(OverlayType type)
{
    return m_Overlays[type].color;
}

void OverlayManager::setOverlayRenderer(IOverlayRenderer* renderer)
{
    m_Renderer = renderer;
}

void OverlayManager::setOverlayPosition(OverlayType type, float x, float y)
{
    m_Overlays[type].x = x;
    m_Overlays[type].y = y;
}

float OverlayManager::getOverlayX(OverlayType type) const
{
    return m_Overlays[type].x;
}

float OverlayManager::getOverlayY(OverlayType type) const
{
    return m_Overlays[type].y;
}

void OverlayManager::notifyOverlayUpdated(OverlayType type)
{
    if (m_Renderer == nullptr) {
        return;
    }

    // Construct the required font to render the overlay
    if (m_Overlays[type].font == nullptr) {
        if (m_FontData.isEmpty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL overlay font failed to load");
            return;
        }

        // m_FontData must stay around until the font is closed
        m_Overlays[type].font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                               1,
                                               m_Overlays[type].fontSize);
        if (m_Overlays[type].font == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_OpenFont() failed: %s",
                        TTF_GetError());

            // Can't proceed without a font
            return;
        }
    }

    // Build the new surface: text rendered on a dark semi-transparent background
    SDL_Surface* newSurface = nullptr;
    if (m_Overlays[type].enabled) {
        // Render text (white, alpha-blended)
        SDL_Surface* textSurface = TTF_RenderText_Blended_Wrapped(
            m_Overlays[type].font,
            m_Overlays[type].text,
            m_Overlays[type].color,
            850);

        if (textSurface != nullptr) {
            // Create a padded background surface in ARGB8888
            const int kPad = 6;
            SDL_Surface* bgSurface = SDL_CreateRGBSurfaceWithFormat(
                0,
                textSurface->w + kPad * 2,
                textSurface->h + kPad * 2,
                32,
                SDL_PIXELFORMAT_ARGB8888);

            if (bgSurface != nullptr) {
                // Dark grey, ~85% opaque (#202020 D9)
                SDL_FillRect(bgSurface, nullptr, SDL_MapRGBA(bgSurface->format, 0x20, 0x20, 0x20, 0xD9));

                // Blit text centred on the padding
                SDL_Rect dstRect = { kPad, kPad, textSurface->w, textSurface->h };
                SDL_BlitSurface(textSurface, nullptr, bgSurface, &dstRect);

                newSurface = bgSurface;
            }

            SDL_FreeSurface(textSurface);
        }
    }

    // Exchange the old surface with the new one
    SDL_Surface* oldSurface = (SDL_Surface*)SDL_AtomicSetPtr(
        (void**)&m_Overlays[type].surface,
        newSurface);

    // Notify the renderer
    m_Renderer->notifyOverlayUpdated(type);

    // Free the old surface
    if (oldSurface != nullptr) {
        SDL_FreeSurface(oldSurface);
    }
}
