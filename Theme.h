#ifndef THEME_H
#define THEME_H

class QApplication;

// Application color theme: follow Windows, always dark or always light.
namespace Theme {

// Console* = mixing-console look (flat grey panels): light grey, dark grey, dark grey + orange
enum Mode { System = 0, Dark = 1, Light = 2, DarkOrange = 3, ConsoleLight = 4, ConsoleDark = 5, ConsoleOrange = 6 };

void init(QApplication &app);   // load saved mode, apply it, watch Windows setting
Mode mode();                    // selected mode
void setMode(Mode m);           // apply immediately and save
bool isDark();                  // theme currently in effect (dark orange counts as dark)
bool isOrange();                // an orange theme (dark orange / console orange) is in effect
bool isConsole();               // one of the console themes is in effect

}

#endif // THEME_H
