#include "wxgui/helpers/wxHelpers.h"

#include <wx/wupdlock.h>
#include <wx/stattext.h>
#include <wx/slider.h>
#include <wx/dirdlg.h>
#include <wx/graphics.h>
#include <wx/settings.h>
#include <wx/dcmemory.h>

#if BOOST_OS_WINDOWS
#include <dwmapi.h>
#pragma comment(lib, "dwmapi")
#endif

#if BOOST_OS_LINUX || BOOST_OS_BSD
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkwindow.h>
#include <gdk/gdkx.h>
#ifdef HAS_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#endif

#include "wxgui/helpers/wxControlObject.h"

void wxAutosizeColumn(wxListCtrlBase* ctrl, int col)
{
	ctrl->SetColumnWidth(col, wxLIST_AUTOSIZE_USEHEADER);
	int wh = ctrl->GetColumnWidth(col);
	ctrl->SetColumnWidth(col, wxLIST_AUTOSIZE);
	int wc = ctrl->GetColumnWidth(col);
	if (wh > wc)
	ctrl->SetColumnWidth(col, wxLIST_AUTOSIZE_USEHEADER);
}

void wxAutosizeColumns(wxListCtrlBase* ctrl, int col_start, int col_end)
{
	wxWindowUpdateLocker lock(ctrl);
	for (int i = col_start; i <= col_end; ++i)
		wxAutosizeColumn(ctrl, i);
}

void update_slider_text(wxCommandEvent& event, const wxFormatString& format /*= "%d%%"*/)
{
	const auto slider = dynamic_cast<wxSlider*>(event.GetEventObject());
	wxASSERT(slider);

	auto slider_text = dynamic_cast<wxControlObject*>(event.GetEventUserData())->GetControl<wxStaticText>();
	wxASSERT(slider_text);

	slider_text->SetLabel(wxString::Format(format, slider->GetValue()));
}

uint32 fix_raw_keycode(uint32 keycode, uint32 raw_flags)
{
#if BOOST_OS_WINDOWS
	const auto flags = (HIWORD(raw_flags) & 0xFFF);
	if(keycode == VK_SHIFT)
	{
		if(flags == 0x2A)
			return 160;
		else if (flags == 0x36)
			return 161;
	}
	else if (keycode == VK_CONTROL)
	{
		if (flags == 0x1d)
			return 162;
		else if (flags == 0x11d)
			return 163;
	}
	else if (keycode == VK_MENU)
	{
		if ((flags & 0xFF) == 0x38)
			return 164;
		else if ((flags & 0xFF) == 0x38)
			return 165;
	}
#endif

	return keycode;
}

WindowSystem::WindowHandleInfo initHandleContextFromWxWidgetsWindow(wxWindow* wxw)
{
	WindowSystem::WindowHandleInfo handleInfo;
#if BOOST_OS_WINDOWS
	handleInfo.backend = WindowSystem::WindowHandleInfo::Backend::Windows;
	handleInfo.surface = reinterpret_cast<void*>(wxw->GetHWND());
#elif BOOST_OS_LINUX || BOOST_OS_BSD
	GtkWidget* gtkWidget = (GtkWidget*)wxw->GetHandle(); // returns GtkWidget
	gtk_widget_realize(gtkWidget);
	GdkWindow* gdkWindow = gtk_widget_get_window(gtkWidget);
	GdkDisplay* gdkDisplay = gdk_window_get_display(gdkWindow);
	if (GDK_IS_X11_WINDOW(gdkWindow))
	{
		handleInfo.backend = WindowSystem::WindowHandleInfo::Backend::X11;
		handleInfo.surface = reinterpret_cast<void*>(gdk_x11_window_get_xid(gdkWindow));
		handleInfo.display = gdk_x11_display_get_xdisplay(gdkDisplay);
		if (!handleInfo.display)
		{
			cemuLog_log(LogType::Force, "Unable to get xlib display");
		}
	}
#ifdef HAS_WAYLAND
	else if (GDK_IS_WAYLAND_WINDOW(gdkWindow))
	{
		handleInfo.backend = WindowSystem::WindowHandleInfo::Backend::Wayland;
		handleInfo.surface = gdk_wayland_window_get_wl_surface(gdkWindow);
		handleInfo.display = gdk_wayland_display_get_wl_display(gdkDisplay);
	}
#endif
	else
	{
		cemuLog_log(LogType::Force, "Unsuported GTK backend");
	}
#elif BOOST_OS_MACOS
	handleInfo.backend = WindowSystem::WindowHandleInfo::Backend::Cocoa;
	handleInfo.surface = reinterpret_cast<void*>(wxw->GetHandle());
#endif
	return handleInfo;
}

wxColour FaroGlyphInkColour()
{
	return wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
}

wxBitmap FaroDrawGlyph(FaroGlyph glyph, int sizePx, const wxColour& colour)
{
	const int size = std::max(sizePx, 8);
	const double s = (double)size;
	// Scales with the icon so the weight looks the same at 16px and at 32px.
	const double stroke = std::max(1.5, s / 11.0);

	wxBitmap bmp(size, size, 32);
	bmp.UseAlpha();
	{
		wxMemoryDC dc(bmp);
		dc.SetBackground(*wxTRANSPARENT_BRUSH);
		dc.Clear();

		std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
		if (!gc)
			return bmp;
		gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
		gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(colour, stroke).Cap(wxCAP_ROUND).Join(wxJOIN_ROUND)));
		gc->SetBrush(*wxTRANSPARENT_BRUSH);

		auto line = [&](double x1, double y1, double x2, double y2) {
			gc->StrokeLine(x1 * s, y1 * s, x2 * s, y2 * s);
		};
		auto dot = [&](double cx, double cy, double r) {
			gc->SetBrush(gc->CreateBrush(wxBrush(colour)));
			gc->DrawEllipse((cx - r) * s, (cy - r) * s, 2 * r * s, 2 * r * s);
			gc->SetBrush(*wxTRANSPARENT_BRUSH);
		};
		auto ring = [&](double cx, double cy, double r) {
			gc->DrawEllipse((cx - r) * s, (cy - r) * s, 2 * r * s, 2 * r * s);
		};
		auto rrect = [&](double x, double y, double w, double h, double r) {
			gc->DrawRoundedRectangle(x * s, y * s, w * s, h * s, r * s);
		};

		switch (glyph)
		{
		case FaroGlyph::Stop:
			gc->SetBrush(gc->CreateBrush(wxBrush(colour)));
			rrect(0.28, 0.28, 0.44, 0.44, 0.07);
			break;
		case FaroGlyph::Load: // arrow descending into a tray
			line(0.5, 0.20, 0.5, 0.58);
			line(0.35, 0.44, 0.5, 0.59);
			line(0.65, 0.44, 0.5, 0.59);
			line(0.20, 0.68, 0.20, 0.80);
			line(0.20, 0.80, 0.80, 0.80);
			line(0.80, 0.80, 0.80, 0.68);
			break;
		case FaroGlyph::Install: // plus
			line(0.5, 0.24, 0.5, 0.76);
			line(0.24, 0.5, 0.76, 0.5);
			break;
		case FaroGlyph::Packs: // stacked layers
			line(0.5, 0.20, 0.82, 0.37);
			line(0.82, 0.37, 0.5, 0.54);
			line(0.5, 0.54, 0.18, 0.37);
			line(0.18, 0.37, 0.5, 0.20);
			line(0.18, 0.58, 0.5, 0.75);
			line(0.5, 0.75, 0.82, 0.58);
			break;
		case FaroGlyph::Settings: // sliders - a cogwheel would imply system config
			line(0.18, 0.36, 0.82, 0.36);
			line(0.18, 0.64, 0.82, 0.64);
			dot(0.64, 0.36, 0.095);
			dot(0.36, 0.64, 0.095);
			break;
		case FaroGlyph::Input: // controller body with two sticks
			rrect(0.14, 0.33, 0.72, 0.34, 0.17);
			dot(0.34, 0.50, 0.075);
			dot(0.66, 0.50, 0.075);
			break;
		case FaroGlyph::Titles: // library grid
			rrect(0.20, 0.20, 0.26, 0.26, 0.05);
			rrect(0.54, 0.20, 0.26, 0.26, 0.05);
			rrect(0.20, 0.54, 0.26, 0.26, 0.05);
			rrect(0.54, 0.54, 0.26, 0.26, 0.05);
			break;
		case FaroGlyph::TabGeneral: // sliders, same idea as Settings
			line(0.18, 0.32, 0.82, 0.32);
			line(0.18, 0.68, 0.82, 0.68);
			dot(0.62, 0.32, 0.09);
			dot(0.38, 0.68, 0.09);
			break;
		case FaroGlyph::TabGraphics: // display
			rrect(0.14, 0.22, 0.72, 0.46, 0.06);
			line(0.36, 0.80, 0.64, 0.80);
			line(0.5, 0.68, 0.5, 0.80);
			break;
		case FaroGlyph::TabAudio: // speaker with one wave
			line(0.26, 0.40, 0.26, 0.60);
			line(0.26, 0.40, 0.44, 0.26);
			line(0.44, 0.26, 0.44, 0.74);
			line(0.44, 0.74, 0.26, 0.60);
			line(0.58, 0.38, 0.58, 0.62);
			line(0.72, 0.30, 0.72, 0.70);
			break;
		case FaroGlyph::TabOverlay: // frame with an inset corner panel
			rrect(0.16, 0.20, 0.68, 0.60, 0.06);
			rrect(0.24, 0.28, 0.26, 0.18, 0.04);
			break;
		case FaroGlyph::TabAccount: // head and shoulders
			ring(0.5, 0.36, 0.145);
			line(0.24, 0.80, 0.24, 0.72);
			line(0.24, 0.72, 0.76, 0.72);
			line(0.76, 0.72, 0.76, 0.80);
			break;
		case FaroGlyph::TabDebug: // terminal prompt
			rrect(0.14, 0.22, 0.72, 0.56, 0.06);
			line(0.26, 0.40, 0.36, 0.50);
			line(0.36, 0.50, 0.26, 0.60);
			line(0.46, 0.62, 0.66, 0.62);
			break;
		}
	}
	return bmp;
}

void FaroApplyRoundedCorners(wxWindow* window)
{
#if BOOST_OS_WINDOWS
	if (!window)
		return;
	// DWM_WINDOW_CORNER_PREFERENCE::DWMWCP_ROUND = 2 (dwmapi.h, Windows 11 SDK).
	int cornerPreference = 2;
	::DwmSetWindowAttribute((HWND)window->GetHandle(), DWMWA_WINDOW_CORNER_PREFERENCE,
		&cornerPreference, sizeof(cornerPreference));
#else
	(void)window;
#endif
}
