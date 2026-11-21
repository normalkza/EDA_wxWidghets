#include <wx/wx.h>
#include"MyApp.h"
#include "MainFrame.h"
#include "StartupDialog.h"
#include "AiSession.h"

bool MyApp::OnInit()
{
	wxInitAllImageHandlers();
	SetExitOnFrameDelete(false);
	{
		StartupDialog login;
		if (login.ShowModal() != wxID_OK) return false;
	}
	MainFrame* frame = new MainFrame();
	SetTopWindow(frame);
	SetExitOnFrameDelete(true);
	frame->Show(true);
	wxWeakRef<MainFrame> owner(frame);
	CallAfter([owner] { if (owner.get()) owner->ShowCompanion(); });

	return true;
}

int MyApp::OnExit()
{
	AiSession::Get().Stop();
	return wxApp::OnExit();
}
