#include <wx/wx.h>
#include"MyApp.h"
#include "MainFrame.h"

bool MyApp::OnInit()
{
	wxInitAllImageHandlers();
	// Create the main application window
	MainFrame* frame = new MainFrame();
	// Show the window
	frame->Show(true);

	return true;
}