# Vision Review

The independent review found that the first delayed-attachment change still depended on Qt event order. The follow-up makes attachment an explicit host action after the tab layout has run and adds an assertion that the popup begins detached. The remaining gap is visual verification in a logged-in WindowServer session.

The core and session suites pass. The real view suite cannot create a screen in this process, so it has not exercised the new popup assertion here.
