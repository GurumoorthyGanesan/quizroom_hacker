/***************************************************************
Module name: HookInjEx_DLL.cpp
Copyright (c) 2003 Robert Kuster

Notice:	If this code works, it was written by Robert Kuster.
		Else, I don't know who wrote it.

		Use it on your own risk. No responsibilities for
		possible damages of even functionality can be taken.
***************************************************************/

#include <windows.h>
#include "HookInjEx_DLL.h"
#include <stdio.h>
extern "C"
{
//-------------------------------------------------------------
// shared data 
// Notice:	seen by both: the instance of "HookInjEx.dll" mapped
//			into "explorer.exe" as well as by the instance
//			of "HookInjEx.dll" mapped into our "HookInjEx.exe"
#pragma data_seg (".shared")
int		g_bSubclassed = 0;	// START button subclassed?
UINT	WM_HOOKEX = 0;
HWND	g_hWnd	= 0;		// handle of START button
HHOOK	g_hHook = 0;
LPCSTR string;
HWND callerHandle=0;
#pragma data_seg ()

#pragma comment(linker,"/SECTION:.shared,RWS")


//-------------------------------------------------------------
// global variables (unshared!)
//
HINSTANCE	hDll;

// New & old window procedure of the subclassed START button
WNDPROC				OldProc = NULL;	
LRESULT CALLBACK	NewProc( HWND,UINT,WPARAM,LPARAM );


//-------------------------------------------------------------
// DllMain
//
BOOL APIENTRY DllMain( HANDLE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved
					 )
{
	if( ul_reason_for_call == DLL_PROCESS_ATTACH ) 
	{
		hDll = (HINSTANCE) hModule;	
		::DisableThreadLibraryCalls( hDll );

		if( WM_HOOKEX==NULL )
			WM_HOOKEX = ::RegisterWindowMessage( "WM_HOOKEX_RK" );			
    }
	
    return TRUE;
}


//-------------------------------------------------------------
// HookProc
// Notice:
// - executed by the instance of "HookInjEx.dll" mapped into "explorer.exe";
//
// When called from InjectDll:
//	  -	sublasses the start button;
//	  -	removes the hook, but the DLL stays in the remote process
//		though, because we increased its reference count via LoadLibray
//		(this way we disturb the target process as litle as possible);
//
// When called from UnmapDll:
//	  -	restores the old window procedure for the start button;
//	  - reduces the reference count of the DLL (via FreeLibrary);
//	  -	removes the hook, so the DLL is unmapped;
//
//		Also note, that the DLL isn't unmapped immediately after the
//		call to UnhookWindowsHookEx, but in the near future
//		(right after finishing with the current message).
//		Actually it's obvious why: windows can NOT unmap the 
//		DLL in the middle of processing a meesage, because the code
//		in the hook procedure is still required. 
//		That's why we can change the order LoadLibrary/FreeLibrary &
//		UnhookWindowsHookEx are called.
//
//		FreeLibrary, in contrast, unmapps the DLL imeditaley if the 
//		reference count reaches zero.
//
#define pCW ((CWPSTRUCT*)lParam)

LRESULT HookProc (
  int code,       // hook code
  WPARAM wParam,  // virtual-key code
  LPARAM lParam   // keystroke-message information
)
{
	if( (pCW->message == WM_HOOKEX) && pCW->lParam ) 
	{
		::UnhookWindowsHookEx( g_hHook );

		if( g_bSubclassed ) 
			goto END;		// already subclassed?
		
		// Let's increase the reference count of the DLL (via LoadLibrary),
		// so it's NOT unmapped once the hook is removed;
		char lib_name[MAX_PATH]; 
		::GetModuleFileName( hDll, lib_name, MAX_PATH );
						
		if( !::LoadLibrary( lib_name ) )
			goto END;		

		// Subclass START button
		OldProc = (WNDPROC) 
			::SetWindowLong( g_hWnd, GWL_WNDPROC, (long)NewProc );
		if( OldProc==NULL )			// failed?
			::FreeLibrary( hDll );
		else {						// success -> leave "HookInjEx.dll"
			::MessageBeep(MB_OK);	// mapped into "explorer.exe"
			g_bSubclassed = true;
		}		
	}
	else if( pCW->message == WM_HOOKEX ) 
	{
		::UnhookWindowsHookEx( g_hHook );

		// Failed to restore old window procedure? => Don't unmap the
		// DLL either. Why? Because then "explorer.exe" would call our
		// "unmapped" NewProc and  crash!!
		if( !SetWindowLong( g_hWnd, GWL_WNDPROC, (long)OldProc ) )
			goto END;

		::FreeLibrary( hDll );

		::MessageBeep(MB_OK);
		g_bSubclassed = false;	
	}

END:
	return ::CallNextHookEx(g_hHook, code, wParam, lParam);
}

LPCSTR GetString()
{
	return string;
}
const char * patternString="\r\n<[BOT]GameServer>";
//-------------------------------------------------------------
// NewProc
// Notice:	- new window procedure for the START button;
//			- it just swaps the left & right muse clicks;
//	
LRESULT CALLBACK NewProc(
  HWND hwnd,      // handle to window
  UINT uMsg,      // message identifier
  WPARAM wParam,  // first message parameter
  LPARAM lParam   // second message parameter
)
{
	if (uMsg==EM_REPLACESEL && g_bSubclassed!=NULL) 
	{
		if(strlen(((char *)lParam))>=19)
		{
			bool match=true;
			for(int i=0;i<19;i++)
			{
				if(patternString[i]!=((char*)lParam)[i])
				{
					match=false;
					break;
				}
			}
			if(match==true)
			{
				COPYDATASTRUCT *dataStruct = new COPYDATASTRUCT;
				dataStruct->dwData=0;
				dataStruct->cbData=strlen((char *)lParam) - 19 ;
				dataStruct->lpData=(char *)lParam+20*sizeof(char);
				//SendMessage(callerHandle,WM_COPYDATA,(WPARAM)g_hWnd,(LPARAM)dataStruct);
				char bab[500];
				SendMessage(callerHandle,WM_COPYDATA,(WPARAM)g_hWnd,(LPARAM)dataStruct);
				delete dataStruct;
			}
		}
	}
	return CallWindowProc( OldProc,hwnd,uMsg,wParam,lParam );
}


//-------------------------------------------------------------
// InjectDll
// Notice: 
//	- injects "HookInjEx.dll" into "explorer.exe" (via SetWindowsHookEx);
//	- subclasses the START button (see HookProc for more details);
//
//		Parameters: - hWnd = START button handle
//
//		Return value:	1 - success;
//						0 - failure;
//
int InjectDll( HWND hWnd ,HWND myAppHwnd)
{	
	g_hWnd = hWnd;
	
	callerHandle=myAppHwnd;

	g_hHook = SetWindowsHookEx( WH_CALLWNDPROC,(HOOKPROC)HookProc,
								hDll, GetWindowThreadProcessId(hWnd,NULL) );
	if( g_hHook==NULL )
		return 0;
	
	// By the time SendMessage returns, 
	// the START button has already been subclassed
	SendMessage( hWnd,WM_HOOKEX,0,1 );

	return g_bSubclassed;
}


//-------------------------------------------------------------
// UnmapDll
// Notice: 
//	- restores the old window procedure for the START button;
//	- unmapps the DLL from the remote process
//	  (see HookProc for more details);
//
//		Return value:	1 - success;
//						0 - failure;
//
int UnmapDll( )
{	
	g_hHook = SetWindowsHookEx( WH_CALLWNDPROC,(HOOKPROC)HookProc,
								hDll, GetWindowThreadProcessId(g_hWnd,NULL) );

	if( g_hHook==NULL )
		return 0;	
	
	SendMessage( g_hWnd,WM_HOOKEX,0,0 );

	return (g_bSubclassed == NULL);
}

}