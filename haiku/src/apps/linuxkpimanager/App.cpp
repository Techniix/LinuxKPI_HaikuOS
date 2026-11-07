/*
 * Copyright 2026, LinuxKPI_HaikuOS contributors.
 * Distributed under the terms of the MIT License.
 */


#include <Application.h>

#include "MainWindow.h"


static const char* kSignature = "application/x-vnd.LinuxKPI-Manager";


class App : public BApplication {
public:
	App()
		:
		BApplication(kSignature)
	{
	}

	virtual void ReadyToRun()
	{
		(new MainWindow())->Show();
	}
};


int
main()
{
	App app;
	app.Run();
	return 0;
}
