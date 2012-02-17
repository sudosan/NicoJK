
#include "stdafx.h"
#include "CommentWindow.h"
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")

// コメント取得用イベント
#define WMS_JKHOST_EVENT (WM_USER + 1)
#define WMS_GETFLV_EVENT (WM_USER + 2)
#define WMS_COMMENT_EVENT (WM_USER + 3)

#ifdef _DEBUG
#include <stdarg.h>
inline void dprintf_real( const _TCHAR * fmt, ... )
{
  _TCHAR buf[1024];
  va_list ap;
  va_start(ap, fmt);
  _vsntprintf_s(buf, 1024, fmt, ap);
  va_end(ap);
  OutputDebugString(buf);
}
#  define dprintf dprintf_real
#else
#  define dprintf __noop
#endif

HFONT CreateFont(int h) {
	return ::CreateFont(h,    //フォント高さ
        0,                    //文字幅
        0,                    //テキストの角度
        0,                    //ベースラインとｘ軸との角度
        FW_SEMIBOLD,            //フォントの重さ（太さ）
        FALSE,                //イタリック体
        FALSE,                //アンダーライン
        FALSE,                //打ち消し線
        SHIFTJIS_CHARSET,    //文字セット
        OUT_DEFAULT_PRECIS,    //出力精度
        CLIP_DEFAULT_PRECIS,//クリッピング精度
        NONANTIALIASED_QUALITY,        //出力品質
        FIXED_PITCH | FF_MODERN,//ピッチとファミリー
        TEXT("MS UI Gothic"));    //書体名
}

class Chat {
public:
	int vpos; // 表示開始vpos
	std::wstring text;
	float line; // 上から0, 1... だけど小数点もあるよ
	int width; // 表示幅（ピクセル）
	Chat(int vpos_in, std::wstring text_in)
		: vpos(vpos_in),
		  text(text_in),
		  line(0),
		  width(200)
	{ }

	Chat(const Chat &c) :
		vpos(c.vpos),
		text(c.text),
		line(c.line),
		width(c.width)
	{ }

	bool operator<(const Chat& b) {
		return vpos < b.vpos;
	}
};

const int VPOS_LEN = 400;

class ChatManager {
	struct filter {
		int minvpos_;
		filter(int minvpos) : minvpos_(minvpos) {}
		bool operator()(const Chat& x) const { return x.vpos < minvpos_; }
	}; 

	int linecount_;
	int window_width_;
	int *lines_;
	int *counts_;

	// GDI
	RECT oldRect_;
	HWND hWnd_;
	HDC hDC_;
	HFONT hFont_;
	HGDIOBJ hOld_;

	void ClearObjects() {
		if (hDC_) {
			SelectObject(hDC_, hOld_);
			DeleteObject(hFont_);
			ReleaseDC(hWnd_, hDC_);
		}
		hDC_ = NULL;
		hWnd_ = NULL;
	}

public:
	std::list<Chat> chat_;

	ChatManager() :
		linecount_(0),
		lines_(NULL),
		counts_(NULL),
		hDC_(NULL),
		hWnd_(NULL)
	{
		SetRectEmpty(&oldRect_);
	}

	bool insert(std::wstring str, int vpos) {
		if (hWnd_) {
			Chat c(vpos, str);
			UpdateChat(c);
			chat_.push_back(Chat(c));
			return true;
		}
		return false;
	}

	bool trim(int minvpos) {
		chat_.remove_if(filter(minvpos));
		return true;
	}

	void UpdateChat(Chat &c) {
		// あいてる列を探す（j 列）
		// TODO: 流れる速度に応じて選ぶ
		int j;
		for (j=0; j<linecount_; j++) {
			if (lines_[j] < c.vpos) {
				break;
			}
		}
		c.line = (float)j;

		// 空きがなかったので適当にvposの小さいとこにずらして表示
		if (j == linecount_) {
			int vpos_min = 0x7fffffff;
			int line_index = 0;
			for (j=0; j<linecount_; j++) {
				if (lines_[j] < vpos_min) {
					vpos_min = lines_[j];
					line_index = j;
				}
			}
			j = line_index;
			c.line = static_cast<float>(0.5 + j);
		}

		SIZE size;
		GetTextExtentPoint32W(hDC_, c.text.c_str(), c.text.length(), &size);
		counts_[j]++;
		c.width = size.cx;
		//dprintf(L"vpod: %d, line: %d, width: %d", c.vpos, c.line, c.width);
		float ratio = (float)size.cx / window_width_;
		double newvpos;
		if (ratio < 0.1) {
			newvpos = c.vpos + static_cast<double>(VPOS_LEN) * (ratio + 0.1);
		} else if (ratio < 0.2) {
			newvpos = c.vpos + static_cast<double>(VPOS_LEN) * (ratio + 0.1);
		} else if (ratio < 0.4) {
			newvpos = c.vpos + static_cast<double>(VPOS_LEN) * 0.5;
		} else if (ratio < 0.6) {
			newvpos = c.vpos + static_cast<double>(VPOS_LEN) * 0.7;
		} else {
			newvpos = c.vpos + static_cast<double>(VPOS_LEN) * 0.9;
		}
		lines_[j] = static_cast<int>(newvpos);
	}

	void Clear() {
		chat_.clear();
		linecount_ = -1;
		delete [] lines_;
		delete [] counts_;
		lines_ = NULL;
		counts_ = NULL;
		ClearObjects();
	}

	void PrepareChats(HWND hWnd) {
		if (!hWnd) {
			return;
		}

		RECT rc;
		GetClientRect(hWnd, &rc);
		if (EqualRect(&rc, &oldRect_)) {
			return;
		}
		CopyRect(&oldRect_, &rc);
		
		window_width_ = rc.right;
		int newLineCount = max(1, rc.bottom / 30 - 1);
		if (linecount_ == newLineCount) {
			return;
		}

		ClearObjects();
		hWnd_ = hWnd;
		hDC_ = GetDC(hWnd);
		hFont_ = CreateFont(28);
		HGDIOBJ hOld = SelectObject(hDC_, hFont_);

		delete [] lines_;
		delete [] counts_;

		linecount_ = newLineCount;
		lines_ = new int[newLineCount];
		counts_ = new int[newLineCount];
		memset(lines_, 0, sizeof(int) * newLineCount);
		memset(counts_, 0, sizeof(int) * newLineCount);

		std::list<Chat>::iterator i;
		for (i=chat_.begin(); i!=chat_.end(); ++i) {
			UpdateChat(*i);
		}
		for (int i=0; i<newLineCount; i++) {
			dprintf(L"line[%d] = %d", i, counts_[i]);
		}
	}
};
ChatManager manager;

Cjk::Cjk(TVTest::CTVTestApp *pApp) :
	m_pApp(pApp),
	hWnd_(NULL),
	memDC_(NULL),
	msPosition_(0)
{ }

void Cjk::Create(HWND hParent) {
	static bool bInitialized = false;
	if (!bInitialized) {
		WNDCLASSEX wc;
		wc.cbSize = sizeof(WNDCLASSEX);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = NULL;
		wc.hIcon = NULL;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = CreateSolidBrush(COLOR_TRANSPARENT);
		wc.lpszMenuName = NULL;
		wc.lpszClassName = TEXT("ru.jk");
		wc.hIconSm = NULL;

		RegisterClassEx(&wc);
		bInitialized = true;
	}

	hWnd_ = CreateWindowEx(
		WS_EX_LAYERED,
		(LPCTSTR)_T("ru.jk"),
		_T("TvtPlayJK"),
		WS_POPUP,
		100, 100,
		200, 200,
		hParent,
		NULL,
		NULL,
		(LPVOID)this);

	HWND hButton = CreateWindow(
		_T("BUTTON"),
		_T("コメント再生開始"),
		WS_CHILD | BS_PUSHBUTTON,
		10, 10,
		200, 30,
		hWnd_,
		(HMENU)(INT_PTR)IDB_START,
		NULL,
		NULL
		);

	ShowWindow(hWnd_, SW_SHOW);
	UpdateWindow(hWnd_);

	Resize(m_pApp->GetAppWindow());
}

void Cjk::Destroy() {
	if (socGetflv_ != INVALID_SOCKET) {
		shutdown(socGetflv_, SD_BOTH);
		closesocket(socComment_);
		socGetflv_ = INVALID_SOCKET;
	}
	if (socComment_ != INVALID_SOCKET) {
		shutdown(socComment_, SD_BOTH);
		closesocket(socComment_);
		socComment_ = INVALID_SOCKET;
	}
	ClearObjects();
	DestroyWindow(hWnd_);
	hWnd_ = NULL;
}

HWND Cjk::GetFullscreenWindow() {
	TVTest::HostInfo hostInfo;
	if (m_pApp->GetFullscreen() && m_pApp->GetHostInfo(&hostInfo)) {
		wchar_t className[64];
		::lstrcpynW(className, hostInfo.pszAppName, 48);
		::lstrcatW(className, L" Fullscreen");
		
		HWND hwnd = NULL;
		while ((hwnd = ::FindWindowEx(NULL, hwnd, className, NULL)) != NULL) {
			DWORD pid;
			::GetWindowThreadProcessId(hwnd, &pid);
			if (pid == ::GetCurrentProcessId()) return hwnd;
		}
	}
	return NULL;
}

void Cjk::Resize(HWND hApp) {
	RECT rc;
	GetWindowRect(hApp, &rc);
	Resize(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
}

void Cjk::Resize(int left, int top, int width, int height) {
	bool fullScreen = m_pApp->GetFullscreen();
	dprintf(L"Resize %x", hWnd_);
	if (fullScreen) {
		HWND hFull = GetFullscreenWindow();
		RECT rc;
		GetWindowRect(hFull, &rc);
		left = rc.left;
		top = rc.top;
		width = rc.right - rc.left;
		height = rc.bottom - rc.top;
		SetWindowPos(hWnd_, HWND_TOPMOST, left + 10, top + 30, width - 20, height - 60, SWP_NOACTIVATE);
	} else {
		if (width && height) {
			SetWindowPos(hWnd_, HWND_NOTOPMOST, left + 10, top + 30, width - 20, height - 60, SWP_NOACTIVATE);
		} else {
			SetWindowPos(hWnd_, HWND_NOTOPMOST, left + 10, top + 30, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
		}
	}
	SetupObjects();
	manager.PrepareChats(hWnd_);
}

void Cjk::SetupObjects() {
	if (memDC_) {
		ClearObjects();
	}

	HDC wndDC = GetDC(hWnd_);
	memDC_ = CreateCompatibleDC(wndDC);

	RECT rc;
	GetClientRect(hWnd_, &rc);
	hBitmap_ = CreateCompatibleBitmap(wndDC, rc.right, rc.bottom);
	if (!hBitmap_) {
		dprintf(L"CreateCompatibleBitmap failed");
	}
	hPrevBitmap_ = (HBITMAP)SelectObject(memDC_, hBitmap_);
	
	ReleaseDC(hWnd_, wndDC);
}

void Cjk::ClearObjects() {
	if (memDC_ == NULL) {
		return ;
	}
	SelectObject(memDC_, hPrevBitmap_);
	DeleteObject(hBitmap_);
	DeleteDC(memDC_);
	memDC_ = NULL;
}

// tokenは ms= のように = もつける。
bool GetToken(const char *in, const char *token, char *out, int len) {
	const char *start = strstr(in, token);
	if (start) {
		start += strlen(token);
		const char *end = strchr(start, '&');
		if (end) {
			strncpy_s(out, len, start, min(end - start, len - 1));
			out[min(end - start, len - 1)] = '\0';
			return true;
		}
	}
	return false;
}

void Cjk::Open(int jkCh) {
	// ウィンドウを一旦壊す
	manager.Clear();
	Destroy();
	Create(m_pApp->GetAppWindow());

	msPositionBase_ = 0;
	jkCh_ = jkCh;

	WSAAsyncGetHostByName(hWnd_, WMS_JKHOST_EVENT, "jk.nicovideo.jp", szHostbuf_, MAXGETHOSTSTRUCT);

	this->SetLiveMode();
	this->Start();
}

void Cjk::Start() {
	msPositionBase_ = msPosition_;
	ShowWindow(GetDlgItem(hWnd_, IDB_START), SW_HIDE);
}

void Cjk::SetLiveMode() {
	msSystime_ = 0;
	msPosition_ = timeGetTime();
}

void Cjk::SetPosition(int posms) {
	msSystime_ = timeGetTime();
	msPosition_ = posms;
}

void Cjk::DrawComments(HWND hWnd, HDC hDC) {
	if (!msPositionBase_) {
		return;
	}
	if (memDC_) {
		SetBkMode(memDC_, TRANSPARENT);
		RECT rc = {0, 0, 1920, 1080};
		HBRUSH transBrush = CreateSolidBrush(COLOR_TRANSPARENT);
		FillRect(memDC_, &rc, transBrush);
		DeleteObject(transBrush);

		RECT rcWnd;
		GetWindowRect(hWnd, &rcWnd);
		int width = rcWnd.right - rcWnd.left;

		int basevpos = (msPosition_ - msPositionBase_ + (timeGetTime() - msSystime_)) / 10;
		SetBkMode(memDC_, TRANSPARENT);
		HGDIOBJ hPrevFont = SelectObject(memDC_, CreateFont(28));
		
		std::list<Chat>::const_iterator i;
		for (i=manager.chat_.cbegin(); i!=manager.chat_.cend(); ++i) {
			// skip head
			if (i->vpos < basevpos - VPOS_LEN) {
				continue;
			}
			// end
			if (i->vpos > basevpos) {
				break;
			}
			int x = static_cast<int>(width - (float)(i->width + width) * (basevpos - i->vpos) / VPOS_LEN);
			int y = static_cast<int>(10 + 30 * i->line);
			const std::wstring &text = i->text;
			SetTextColor(memDC_, RGB(0, 0, 0));
			TextOutW(memDC_, x+2, y+2, text.c_str(), text.length());
			SetTextColor(memDC_, RGB(255, 255, 255));
			TextOutW(memDC_, x, y, text.c_str(), text.length());
		}
		DeleteObject(SelectObject(memDC_, hPrevFont));

		BitBlt(hDC, 0, 0, rcWnd.right - rcWnd.left, rc.bottom - rc.top, memDC_, 0, 0, SRCCOPY);
	}
}

LRESULT CALLBACK Cjk::WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
	HDC hdc;
	PAINTSTRUCT ps;

	static Cjk *pThis = NULL;

	switch (msg) {
	case WM_CREATE:
		SetLayeredWindowAttributes(hWnd, COLOR_TRANSPARENT, 0, LWA_COLORKEY);
		pThis = (Cjk*)((CREATESTRUCT*)lp)->lpCreateParams;
		SetTimer(hWnd, jkTimerID, 50, NULL);
		pThis->socGetflv_ = INVALID_SOCKET;
		pThis->socComment_ = INVALID_SOCKET;
		manager.PrepareChats(hWnd);
		break;
	case WM_COMMAND:
		switch(LOWORD(wp)) {
		case IDB_START:
			pThis->Start();
			break;
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		pThis->DrawComments(hWnd, hdc);
		EndPaint(hWnd, &ps);
		break;
	case WM_TIMER:
		hdc = GetDC(hWnd);
		pThis->DrawComments(hWnd, hdc);
		ReleaseDC(hWnd, hdc);
		break;
	case WM_CLOSE:
		PostMessage(GetParent(hWnd), WM_CLOSE, 0, 0);
		DestroyWindow(hWnd);
		break;
	case WMS_JKHOST_EVENT:
		if (WSAGETASYNCERROR(lp) == 0) {
			HOSTENT *hostent = (HOSTENT*)pThis->szHostbuf_;
			OutputDebugString(_T("JKHOST_EVENT"));
			pThis->socGetflv_ = socket(PF_INET, SOCK_STREAM, 0);
			if (pThis->socGetflv_ == INVALID_SOCKET) {
				//error
			}
			WSAAsyncSelect(pThis->socGetflv_, hWnd, WMS_GETFLV_EVENT, FD_CONNECT | FD_WRITE | FD_READ | FD_CLOSE);
			
			struct sockaddr_in serversockaddr;
			serversockaddr.sin_family = AF_INET;
			serversockaddr.sin_addr.s_addr = *(UINT*)(hostent->h_addr);
			serversockaddr.sin_port = htons(80);
			memset(serversockaddr.sin_zero, 0, sizeof(serversockaddr.sin_zero));
			if (connect(pThis->socGetflv_, (struct sockaddr*)&serversockaddr, sizeof(serversockaddr)) == SOCKET_ERROR) {
				if (WSAGetLastError() != WSAEWOULDBLOCK) {
					MessageBox(hWnd, _T("ニコニコサーバへの接続に失敗しました。"), _T("Error"), MB_OK | MB_ICONERROR);
				}
			}
		}
		return TRUE;
	case WMS_GETFLV_EVENT:
		{
			SOCKET soc = (SOCKET)wp;
			char *szGetTemplate = "GET /api/getflv?v=jk%d HTTP/1.0\r\nHost: jk.nicovideo.jp\r\n\r\n";
			switch(WSAGETSELECTEVENT(lp)) {
			case FD_CONNECT:		/* サーバ接続したイベントの場合 */
				OutputDebugString(_T("getflv Connected"));
				break;
			case FD_WRITE:
				OutputDebugString(_T("getflv Write"));
				char szGet[1024];
				wsprintfA(szGet, szGetTemplate, pThis->jkCh_);
				send(soc, szGet, strlen(szGet)+1, 0);
				break;
			case FD_READ:
				char buf[10240];
				int read;
				read = recv(soc, buf, sizeof(buf)-1, 0);
				buf[read] = '\0';
				if (strstr(buf, "done=true")) {
					char szMs[1024], szMsPort[1024];
					if (GetToken(buf, "ms=", szMs, 1024)
							&& GetToken(buf, "ms_port=", szMsPort, 1024)
							&& GetToken(buf, "thread_id=", pThis->szThread_, 1024)) {
						pThis->socComment_ = socket(PF_INET, SOCK_STREAM, 0);
						if (pThis->socComment_ == INVALID_SOCKET) {
							//error
						}
						WSAAsyncSelect(pThis->socComment_, hWnd, WMS_COMMENT_EVENT, FD_CONNECT | FD_WRITE | FD_READ | FD_CLOSE);

						struct sockaddr_in serversockaddr;
						serversockaddr.sin_family = AF_INET;
						serversockaddr.sin_addr.s_addr = inet_addr(szMs);
						serversockaddr.sin_port = htons((unsigned short)atoi(szMsPort));
						memset(serversockaddr.sin_zero, 0, sizeof(serversockaddr.sin_zero));

						// コメントサーバに接続
						if (connect(pThis->socComment_, (struct sockaddr*)&serversockaddr, sizeof(serversockaddr)) == SOCKET_ERROR) {
							if (WSAGetLastError() != WSAEWOULDBLOCK) {
								MessageBox(hWnd, _T("コメントサーバへの接続に失敗しました。"), _T("Error"), MB_OK | MB_ICONERROR);
							}
						}
					}
				}
				OutputDebugStringA(buf);
				shutdown(pThis->socGetflv_, SD_BOTH);
				break;
			case FD_CLOSE:
				OutputDebugString(_T("getflv Closed"));
				closesocket(soc);
				pThis->socGetflv_ = INVALID_SOCKET;
				break;
			}
		}
		return TRUE;
	case WMS_COMMENT_EVENT:
		{
			SOCKET soc = (SOCKET)wp;
			const char *szRequestTemplate = "<thread res_from=\"-10\" version=\"20061206\" thread=\"%s\" />";
			switch(WSAGETSELECTEVENT(lp)) {
			case FD_CONNECT:		/* サーバ接続したイベントの場合 */
				OutputDebugString(_T("Comment Connected"));
				manager.PrepareChats(hWnd);
				break;
			case FD_WRITE:
				OutputDebugString(_T("Comment Write"));
				char szRequest[1024];
				wsprintfA(szRequest, szRequestTemplate, pThis->szThread_);
				send(soc, szRequest, strlen(szRequest) + 1, 0); // '\0'まで送る
				break;
			case FD_READ:
				char buf[10240];
				int read;
				read = recv(soc, buf, sizeof(buf)-1, 0);
				wchar_t wcsBuf[10240];
				int wcsLen;
				wcsLen = MultiByteToWideChar(CP_UTF8, 0, buf, read, wcsBuf, 10240);
				int pos;
				pos = 0;
				do {
					wchar_t *line = &wcsBuf[pos];
					int len = wcslen(line);
					if (wcsncmp(line, L"<chat ", 6) == 0) {
						wchar_t *es = wcschr(line, L'>');
						wchar_t *ee = wcschr(line + 1, L'<');
						if (es && ee) {
							++es;
							*ee = L'\0';
							dprintf(es);
							manager.insert(es, timeGetTime() / 10);
						}
					}
					pos += len + 1;
				} while(pos < wcsLen);
				// 古いのは消す
				manager.trim(timeGetTime() / 10 - VPOS_LEN * 3);
				break;
			case FD_CLOSE:
				OutputDebugString(_T("Comment Closed"));
				closesocket(pThis->socComment_);
				pThis->socComment_ = INVALID_SOCKET;
				break;
			}
		}
		return TRUE;
		default:
			return (DefWindowProc(hWnd, msg, wp, lp));
	}
	return 0;
}

BOOL Cjk::WindowMsgCallback(HWND hwnd,UINT uMsg,WPARAM wParam,LPARAM lParam,LRESULT *pResult) {
	if (!hWnd_) {
		return FALSE;
	}

    switch (uMsg) {
	case WM_ACTIVATE:
		if (WA_INACTIVE == LOWORD(wParam)) {
			return FALSE;
		}
	case WM_MOVE:
    case WM_SIZE:
		Resize(m_pApp->GetAppWindow());
		break;
	}
	return FALSE;
}

LRESULT Cjk::EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2) {
	if (!hWnd_) {
		return FALSE;
	}

	switch (Event) {
    case TVTest::EVENT_PLUGINENABLE:
        // プラグインの有効状態が変化した
        //return pThis->EnablePlugin(lParam1 != 0);
		break;
    case TVTest::EVENT_FULLSCREENCHANGE:
        // 全画面表示状態が変化した
        if (m_pApp->IsPluginEnabled()) {
            Resize(m_pApp->GetAppWindow());
		}
        break;
	}
	return 0;
}
