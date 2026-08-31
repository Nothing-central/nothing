file(REMOVE_RECURSE
  "SabreBrowser/ui/components/AddressBar.qml"
  "SabreBrowser/ui/components/NewTabPage.qml"
  "SabreBrowser/ui/components/TabBar.qml"
  "SabreBrowser/ui/components/Toolbar.qml"
  "SabreBrowser/ui/pages/IncognitoWindow.qml"
  "SabreBrowser/ui/pages/NormalWindow.qml"
  "SabreBrowser/ui/pages/SplashScreen.qml"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/sabre-browser_tooling.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
