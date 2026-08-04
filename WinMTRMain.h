#ifndef WINMTRMAIN_H_
#define WINMTRMAIN_H_

class WinMTRMain : public CWinApp
{
public:
    WinMTRMain();
    virtual BOOL InitInstance();
    virtual int ExitInstance();
    DECLARE_MESSAGE_MAP()
};

#endif // WINMTRMAIN_H_
