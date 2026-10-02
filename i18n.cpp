#include "i18n.h"

#include "Resource.h"

namespace {

constexpr const wchar_t* kEn[static_cast<int>(StrId::Count)] = {
    L"Barglow",
    L"Barglow",
    L"&Settings...",
    L"&About...",
    L"E&xit",
    L"Barglow Settings",
    L"Strength",
    L"Quiet",
    L"Present",
    L"Loud",
    L"Theme",
    L"Follow Windows",
    L"Force dark",
    L"Force light",
    L"Language",
    L"Follow system",
    L"English",
    L"Simplified Chinese",
    L"Enable visualizer",
    L"Start with Windows",
    L"Close",
    L"About Barglow",
    L"Taskbar audio visualizer for Windows 11.",
    L"Version 1.0.0",
    L"by HUANG Cheng",
    L"Repo: <a href=\"https://github.com/huangcheng/Barglow\">github.com/huangcheng/Barglow</a>",
    L"OK",
};

// Simplified Chinese; author credit keeps canonical Latin name HUANG Cheng.
constexpr const wchar_t* kZhHans[static_cast<int>(StrId::Count)] = {
    L"Barglow",
    L"Barglow",
    L"设置(&S)...",
    L"关于(&A)...",
    L"退出(&X)",
    L"Barglow 设置",
    L"强度",
    L"轻柔",
    L"适中",
    L"响亮",
    L"主题",
    L"跟随系统",
    L"强制深色",
    L"强制浅色",
    L"语言",
    L"跟随系统",
    L"English",
    L"简体中文",
    L"启用可视化",
    L"开机启动",
    L"关闭",
    L"关于 Barglow",
    L"适用于 Windows 11 的任务栏音频可视化工具。",
    L"版本 1.0.0",
    L"作者：HUANG Cheng",
    L"仓库：<a href=\"https://github.com/huangcheng/Barglow\">github.com/huangcheng/Barglow</a>",
    L"确定",
};

UiLang g_lang = UiLang::English;

}  // namespace

UiLang DetectSystemUiLang() {
  const LANGID lang = GetUserDefaultUILanguage();
  return PRIMARYLANGID(lang) == LANG_CHINESE ? UiLang::SimplifiedChinese
                                             : UiLang::English;
}

void ApplyLanguagePreference(LanguagePreference pref) {
  switch (pref) {
    case LanguagePreference::English:
      g_lang = UiLang::English;
      break;
    case LanguagePreference::SimplifiedChinese:
      g_lang = UiLang::SimplifiedChinese;
      break;
    case LanguagePreference::FollowSystem:
    default:
      g_lang = DetectSystemUiLang();
      break;
  }
}

UiLang CurrentUiLang() { return g_lang; }

const wchar_t* Tr(StrId id) {
  const int i = static_cast<int>(id);
  if (i < 0 || i >= static_cast<int>(StrId::Count)) return L"";
  return g_lang == UiLang::SimplifiedChinese ? kZhHans[i] : kEn[i];
}

void ApplyDialogIcon(HWND hwnd, HINSTANCE instance) {
  if (!hwnd || !instance) return;

  HICON iconBig = reinterpret_cast<HICON>(LoadImageW(
      instance, MAKEINTRESOURCEW(IDI_BARGLOW), IMAGE_ICON,
      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
  HICON iconSm = reinterpret_cast<HICON>(LoadImageW(
      instance, MAKEINTRESOURCEW(IDI_BARGLOW), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  if (!iconSm) {
    iconSm = reinterpret_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_SMALL), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  }

  if (iconBig) {
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconBig));
  }
  if (iconSm) {
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSm));
  }
}

void LocalizeMenu(HMENU menu) {
  if (!menu) return;
  HMENU popup = GetSubMenu(menu, 0);
  if (!popup) popup = menu;
  ModifyMenuW(popup, IDM_SETTINGS, MF_BYCOMMAND | MF_STRING, IDM_SETTINGS,
              Tr(StrId::MenuSettings));
  ModifyMenuW(popup, IDM_ABOUT, MF_BYCOMMAND | MF_STRING, IDM_ABOUT, Tr(StrId::MenuAbout));
  ModifyMenuW(popup, IDM_EXIT, MF_BYCOMMAND | MF_STRING, IDM_EXIT, Tr(StrId::MenuExit));
}
