#pragma once

#include "interface/WindowSystem.h"
#include <wx/control.h>
#include <wx/listbase.h>
#include <wx/string.h>

template <>
struct fmt::formatter<wxString> : formatter<string_view>
{
	template <typename FormatContext>
	auto format(const wxString& str, FormatContext& ctx) const
	{
		return formatter<string_view>::format(str.ToStdString(), ctx);
	}
};

class wxTempEnable
{
public:
	wxTempEnable(wxControl* control, bool state = true)
		: m_control(control), m_state(state)
	{
		m_control->Enable(m_state);
	}

	wxTempEnable(const wxTempEnable&) = delete;
	wxTempEnable(wxTempEnable&&) noexcept = default;

	~wxTempEnable()
	{
		if(m_control)
			m_control->Enable(!m_state);
	}

private:
	wxControl* m_control;
	const bool m_state;
};

class wxTempDisable : wxTempEnable
{
public:
	wxTempDisable(wxControl* control)
		: wxTempEnable(control, false) {}
};

template<typename ...TArgs>
wxString formatWxString(const wxString& format, TArgs&&...args)
{
	return wxString::FromUTF8(fmt::format(fmt::runtime(format.utf8_string()), std::forward<TArgs>(args)...));
}

// executes a function when destroying the obj
template<typename TFunction, typename ...TArgs>
class wxDTorFunc
{
public:
	wxDTorFunc(TFunction&& func, TArgs&&... args)
	{
		auto bound = std::bind(std::forward<TFunction>(func), std::forward<TArgs>(args)...);
		// m_function = [bound] { bound(); };
		m_function = [bound{ std::move(bound) }]{ bound(); };
		
	}
	~wxDTorFunc()
	{
		m_function();
	}
private:
	std::function<void()> m_function;
};

void wxAutosizeColumn(wxListCtrlBase* ctrl, int col);
void wxAutosizeColumns(wxListCtrlBase* ctrl, int col_start, int col_end);

template <typename T>
T get_next_sibling(const T element)
{
	static_assert(std::is_pointer_v<T> && std::is_base_of_v<wxControl, std::remove_pointer_t<T>>, "element must be a pointer and inherit from wxControl");
	for (auto sibling = element->GetNextSibling(); sibling; sibling = sibling->GetNextSibling())
	{
		if (auto result = dynamic_cast<T>(sibling))
			return result;
	}

	return nullptr;
}

template <typename T>
T get_prev_sibling(const T element)
{
	static_assert(std::is_pointer_v<T> && std::is_base_of_v<wxControl, std::remove_pointer_t<T>>, "element must be a pointer and inherit from wxControl");
	for (auto sibling = element->GetPrevSibling(); sibling; sibling = sibling->GetPrevSibling())
	{
		auto result = dynamic_cast<T>(sibling);
		if (result)
			return result;
	}

	return nullptr;
}

void update_slider_text(wxCommandEvent& event, const wxFormatString& format = "%d%%");

// Faro: minimal line-art icons drawn with wxGraphicsContext instead of loaded
// from image files.
//
// The platform's stock art and this project's own .ico set both look dated;
// FontAwesome is embedded but only reachable from ImGui, which takes it as an
// in-memory TTF (getting that into a wxFont needs AddFontMemResourceEx on
// Windows and a different path elsewhere); and shipping a new icon set means
// new binary assets to carry. Drawing them costs none of that: each glyph is
// expressed in a 0..1 box and multiplied by the requested size, so it is crisp
// at any display scaling rather than resampled, and the colour is passed in so
// the caller can follow the system theme.
enum class FaroGlyph
{
	// Main window toolbar
	Stop,
	Load,
	Install,
	Packs,
	Settings,
	Input,
	Titles,
	// Settings dialog tabs
	TabGeneral,
	TabGraphics,
	TabAudio,
	TabOverlay,
	TabAccount,
	TabDebug,
};

wxBitmap FaroDrawGlyph(FaroGlyph glyph, int sizePx, const wxColour& colour);
// The theme's own foreground, so the glyphs stay legible under a dark theme
// instead of coming out black on near-black.
wxColour FaroGlyphInkColour();

// Faro: asks DWM to round this window's corners.
//
// Windows 11 already rounds top-level windows by default, so this mainly makes
// the choice explicit and forces the full radius instead of letting DWM pick.
// It only affects TOP-LEVEL windows: DWM does not round child controls, so the
// group boxes and panels inside a dialog are unaffected and would have to be
// custom-drawn to look rounded.
//
// A no-op on Windows 10 and on Windows 11 before build 22000, where
// DwmSetWindowAttribute returns a non-S_OK HRESULT that is deliberately
// ignored - that failure is itself the fallback (square corners, as before).
void FaroApplyRoundedCorners(wxWindow* window);

uint32 fix_raw_keycode(uint32 keycode, uint32 raw_flags);

WindowSystem::WindowHandleInfo initHandleContextFromWxWidgetsWindow(wxWindow* wxw);
