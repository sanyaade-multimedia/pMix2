#include "../JuceLibraryCode/JuceHeader.h"
#include "MainHostWindow.h"
#include "InternalFilters.h"

static double snapToIntegerZoom (double zoom)
{
  if (zoom >= 1.0)
    return (double) (int) (zoom + 0.5);
  
  return 1.0 / (int) (1.0 / zoom + 0.5);
}

class MainHostWindow::PluginListWindow  : public DocumentWindow
{
public:
  PluginListWindow (MainHostWindow& owner_, AudioPluginFormatManager& formatManager)
    : DocumentWindow ("Available Plugins", Colours::white,
                      DocumentWindow::minimiseButton | DocumentWindow::closeButton),
    owner (owner_)
  {
    const File deadMansPedalFile (getAppProperties().getUserSettings()
                                  ->getFile().getSiblingFile ("RecentlyCrashedPluginsList"));

    setContentOwned (new PluginListComponent (formatManager,
                     owner.knownPluginList,
                     deadMansPedalFile,
                     getAppProperties().getUserSettings()), true);

    setResizable (true, false);
    setResizeLimits (300, 400, 800, 1500);
    setTopLeftPosition (60, 60);

    restoreWindowStateFromString (getAppProperties().getUserSettings()->getValue ("listWindowPos"));
    setVisible (true);
  }

  ~PluginListWindow()
  {
    getAppProperties().getUserSettings()->setValue ("listWindowPos", getWindowStateAsString());

    clearContentComponent();
  }

  void closeButtonPressed()
  {
    owner.pluginListWindow = nullptr;
  }

private:
  MainHostWindow& owner;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginListWindow)
};

MainHostWindow::MainHostWindow()
  : DocumentWindow (JUCEApplication::getInstance()->getApplicationName(), Colours::lightgrey,
                    DocumentWindow::allButtons)
{
  formatManager.addDefaultFormats();
  formatManager.addFormat (new InternalPluginFormat());

  ScopedPointer<XmlElement> savedAudioState (getAppProperties().getUserSettings()
      ->getXmlValue ("audioDeviceState"));

  deviceManager.initialise (256, 256, savedAudioState, true);

  setResizable (true, false);
  setResizeLimits (500, 400, 10000, 10000);
  centreWithSize (800, 600);

  setContentOwned (new GraphDocumentComponent (formatManager, &deviceManager), false);

  restoreWindowStateFromString (getAppProperties().getUserSettings()->getValue ("mainWindowPos"));

  setVisible (true);

  InternalPluginFormat internalFormat;
  internalFormat.getAllTypes (internalTypes);

  ScopedPointer<XmlElement> savedPluginList (getAppProperties().getUserSettings()->getXmlValue ("pluginList"));

  if (savedPluginList != nullptr)
    knownPluginList.recreateFromXml (*savedPluginList);

  pluginSortMethod = (KnownPluginList::SortMethod) getAppProperties().getUserSettings()
                     ->getIntValue ("pluginSortMethod", KnownPluginList::sortByManufacturer);

  knownPluginList.addChangeListener (this);

  addKeyListener (getCommandManager().getKeyMappings());

  Process::setPriority (Process::HighPriority);
  
  getCommandManager().registerAllCommandsForTarget (this);

  PopupMenu pop;
  pop.addCommandItem (&getCommandManager(), CommandIDs::aboutBox);
  pop.addSeparator();
  pop.addCommandItem (&getCommandManager(), CommandIDs::showAudioSettings);
  pop.addSeparator();
  
#if JUCE_MAC
  setMacMainMenu (this, &pop);
#else
  setMenuBar (this);
#endif
  
  getCommandManager().setFirstCommandTarget (this);
}

MainHostWindow::~MainHostWindow()
{
  pluginListWindow = nullptr;

#if JUCE_MAC
  setMacMainMenu (nullptr);
#else
  setMenuBar (nullptr);
#endif

  knownPluginList.removeChangeListener (this);

  getAppProperties().getUserSettings()->setValue ("mainWindowPos", getWindowStateAsString());
  clearContentComponent();
}

void MainHostWindow::closeButtonPressed()
{
  tryToQuitApplication();
}

bool MainHostWindow::tryToQuitApplication()
{
  PluginWindow::closeAllCurrentlyOpenWindows();

  if (getGraphEditor() == nullptr
      || getGraphEditor()->graph.saveIfNeededAndUserAgrees() == FileBasedDocument::savedOk)
  {
    JUCEApplication::quit();
    return true;
  }

  return false;
}

void MainHostWindow::changeListenerCallback (ChangeBroadcaster*)
{
  menuItemsChanged();

  // save the plugin list every time it gets chnaged, so that if we're scanning
  // and it crashes, we've still saved the previous ones
  ScopedPointer<XmlElement> savedPluginList (knownPluginList.createXml());

  if (savedPluginList != nullptr)
  {
    getAppProperties().getUserSettings()->setValue ("pluginList", savedPluginList);
    getAppProperties().saveIfNeeded();
  }
}

StringArray MainHostWindow::getMenuBarNames()
{
  const char* const names[] = { "File", "Edit", "Plugins", "Options", nullptr };

  return StringArray (names);
}

PopupMenu MainHostWindow::getMenuForIndex (int topLevelMenuIndex, const String& /*menuName*/)
{
  PopupMenu menu;

  if (topLevelMenuIndex == 0)
  {
    // "File" menu
    menu.addCommandItem (&getCommandManager(), CommandIDs::open);

    RecentlyOpenedFilesList recentFiles;
    recentFiles.restoreFromString (getAppProperties().getUserSettings()
                                   ->getValue ("recentFilterGraphFiles"));

    PopupMenu recentFilesMenu;
    recentFiles.createPopupMenuItems (recentFilesMenu, 100, true, true);
    menu.addSubMenu ("Open recent file", recentFilesMenu);

    menu.addCommandItem (&getCommandManager(), CommandIDs::save);
    menu.addCommandItem (&getCommandManager(), CommandIDs::saveAs);
    //menu.addSeparator();
    //menu.addCommandItem (&getCommandManager(), StandardApplicationCommandIDs::quit);
  }
  else if (topLevelMenuIndex == 1)
  {
    menu.addCommandItem (&getCommandManager(), CommandIDs::copy);
    menu.addCommandItem (&getCommandManager(), CommandIDs::paste);
    menu.addSeparator();
    menu.addCommandItem (&getCommandManager(), CommandIDs::undo);
    menu.addCommandItem (&getCommandManager(), CommandIDs::redo);
  }
  else if (topLevelMenuIndex == 2)
  {
    // "Plugins" menu
    PopupMenu pluginsMenu;
    addPluginsToMenu (pluginsMenu);
    menu.addSubMenu ("Create plugin", pluginsMenu);
    menu.addSeparator();
    menu.addItem (250, "Delete all plugins");
  }
  else if (topLevelMenuIndex == 3)
  {
    // "Options" menu

    menu.addCommandItem (&getCommandManager(), CommandIDs::showPluginListEditor);

    PopupMenu sortTypeMenu;
    sortTypeMenu.addItem (200, "List plugins in default order",      true, pluginSortMethod == KnownPluginList::defaultOrder);
    sortTypeMenu.addItem (201, "List plugins in alphabetical order", true, pluginSortMethod == KnownPluginList::sortAlphabetically);
    sortTypeMenu.addItem (202, "List plugins by category",           true, pluginSortMethod == KnownPluginList::sortByCategory);
    sortTypeMenu.addItem (203, "List plugins by manufacturer",       true, pluginSortMethod == KnownPluginList::sortByManufacturer);
    sortTypeMenu.addItem (204, "List plugins based on the directory structure", true, pluginSortMethod == KnownPluginList::sortByFileSystemLocation);
    menu.addSubMenu ("Plugin menu type", sortTypeMenu);
    
    menu.addSeparator();
    menu.addCommandItem (&getCommandManager(), CommandIDs::zoomIn);
    menu.addCommandItem (&getCommandManager(), CommandIDs::zoomOut);
    menu.addCommandItem (&getCommandManager(), CommandIDs::zoomNormal);
  }

  return menu;
}

void MainHostWindow::menuItemSelected (int menuItemID, int /*topLevelMenuIndex*/)
{
  GraphDocumentComponent* const graphEditor = getGraphEditor();

  if (menuItemID == 250)
  {
    if (graphEditor != nullptr)
      graphEditor->graph.clear();
  }
  else if (menuItemID >= 100 && menuItemID < 200)
  {
    RecentlyOpenedFilesList recentFiles;
    recentFiles.restoreFromString (getAppProperties().getUserSettings()
                                   ->getValue ("recentFilterGraphFiles"));

    if (graphEditor != nullptr && graphEditor->graph.saveIfNeededAndUserAgrees() == FileBasedDocument::savedOk)
      graphEditor->graph.loadFrom (recentFiles.getFile (menuItemID - 100), true);
  }
  else if (menuItemID >= 200 && menuItemID < 210)
  {
    if (menuItemID == 200)     pluginSortMethod = KnownPluginList::defaultOrder;
    else if (menuItemID == 201)     pluginSortMethod = KnownPluginList::sortAlphabetically;
    else if (menuItemID == 202)     pluginSortMethod = KnownPluginList::sortByCategory;
    else if (menuItemID == 203)     pluginSortMethod = KnownPluginList::sortByManufacturer;
    else if (menuItemID == 204)     pluginSortMethod = KnownPluginList::sortByFileSystemLocation;

    getAppProperties().getUserSettings()->setValue ("pluginSortMethod", (int) pluginSortMethod);

    menuItemsChanged();
  }
//  else
//  {
//    createPlugin (getChosenType (menuItemID),
//                  proportionOfWidth  (0.3f + Random::getSystemRandom().nextFloat() * 0.6f),
//                  proportionOfHeight (0.3f + Random::getSystemRandom().nextFloat() * 0.6f));
//  }
}

void MainHostWindow::createPlugin (const PluginDescription* desc, int x, int y)
{
  GraphDocumentComponent* const graphEditor = getGraphEditor();

  if (graphEditor != nullptr)
    graphEditor->createNewPlugin (desc, x, y);
}

void MainHostWindow::addPluginsToMenu (PopupMenu& m) const
{
  for (int i = 0; i < internalTypes.size(); ++i)
    m.addItem (i + 1, internalTypes.getUnchecked(i)->name);

  m.addSeparator();

  knownPluginList.addToMenu (m, pluginSortMethod);
}

const PluginDescription* MainHostWindow::getChosenType (const int menuID) const
{
  if (menuID >= 1 && menuID < 1 + internalTypes.size())
    return internalTypes [menuID - 1];

  return knownPluginList.getType (knownPluginList.getIndexChosenByMenu (menuID));
}

ApplicationCommandTarget* MainHostWindow::getNextCommandTarget()
{
  return findFirstTargetParentComponent();
}

void MainHostWindow::getAllCommands (Array <CommandID>& commands)
{
  // this returns the set of all commands that this target can perform..
  const CommandID ids[] = { 
                            CommandIDs::aboutBox,
                            CommandIDs::open,
                            CommandIDs::save,
                            CommandIDs::saveAs,
                            CommandIDs::showPluginListEditor,
                            CommandIDs::showAudioSettings,
                            CommandIDs::copy,
                            CommandIDs::paste,
                            CommandIDs::undo,
                            CommandIDs::redo,
                            CommandIDs::zoomIn,
                            CommandIDs::zoomOut,
                            CommandIDs::zoomNormal
                          };

  commands.addArray (ids, numElementsInArray (ids));
}

void MainHostWindow::getCommandInfo (const CommandID commandID, ApplicationCommandInfo& result)
{
  const String category ("General");

  switch (commandID)
  {
    case CommandIDs::open:
      result.setInfo ("Open...",
                      "Opens a filter graph file",
                      category, 0);
      result.defaultKeypresses.add (KeyPress ('o', ModifierKeys::commandModifier, 0));
      break;

    case CommandIDs::save:
      result.setInfo ("Save",
                      "Saves the current graph to a file",
                      category, 0);
      result.defaultKeypresses.add (KeyPress ('s', ModifierKeys::commandModifier, 0));
      break;

    case CommandIDs::saveAs:
      result.setInfo ("Save As...",
                      "Saves a copy of the current graph to a file",
                      category, 0);
      result.defaultKeypresses.add (KeyPress ('s', ModifierKeys::shiftModifier | ModifierKeys::commandModifier, 0));
      break;

    case CommandIDs::showPluginListEditor:
      result.setInfo ("Edit the list of available plug-Ins...", String::empty, category, 0);
      result.addDefaultKeypress ('p', ModifierKeys::commandModifier);
      break;

    case CommandIDs::showAudioSettings:
      result.setInfo ("Preferences...", String::empty, category, 0);
      result.addDefaultKeypress (',', ModifierKeys::commandModifier);
      break;

    case CommandIDs::aboutBox:
      result.setInfo ("About pMix", String::empty, category, 0);
      break;
      
    case CommandIDs::copy:
      result.setInfo ("Copy",
                      "Copies the currently selected filter to the clipboard",
                      category, 0);
      result.defaultKeypresses.add (KeyPress ('c', ModifierKeys::commandModifier, 0));
      
      break;
    case CommandIDs::paste:
      result.setInfo ("Paste",
                      "Pastes from the clipboard",
                      category, 0);
      result.defaultKeypresses.add (KeyPress ('p', ModifierKeys::commandModifier, 0));
      
      break;
    case CommandIDs::undo:
      result.setInfo ("Undo",
                      "Undo the last action",
                      category, 0);
      result.defaultKeypresses.add (KeyPress ('z', ModifierKeys::commandModifier, 0));
      
      break;
    case CommandIDs::redo:
      result.setInfo ("Redo",
                      "Redo the last action",
                      category, 0);
      result.defaultKeypresses.add (KeyPress ('b', ModifierKeys::commandModifier, 0));
      break;
    case CommandIDs::zoomIn:
      result.setInfo (TRANS("Zoom in"), TRANS("Zooms in on the current component."), category, 0);
//      result.setActive (currentPaintRoutine != nullptr || currentLayout != nullptr);
      result.defaultKeypresses.add (KeyPress (']', ModifierKeys::commandModifier, 0));
      break;
      
    case CommandIDs::zoomOut:
      result.setInfo (TRANS("Zoom out"), TRANS("Zooms out on the current component."), category, 0);
//      result.setActive (currentPaintRoutine != nullptr || currentLayout != nullptr);
      result.defaultKeypresses.add (KeyPress ('[', ModifierKeys::commandModifier, 0));
      break;
      
    case CommandIDs::zoomNormal:
      result.setInfo (TRANS("Zoom to 100%"), TRANS("Restores the zoom level to normal."), category, 0);
//      result.setActive (currentPaintRoutine != nullptr || currentLayout != nullptr);
      result.defaultKeypresses.add (KeyPress ('1', ModifierKeys::commandModifier, 0));
      break;
      //TODO: use these:
//    case StandardApplicationCommandIDs::undo:
//      break;
//      
//    case StandardApplicationCommandIDs::redo:
//      break;
//      
//    case StandardApplicationCommandIDs::cut:
//      break;
//      
//    case StandardApplicationCommandIDs::copy:
//      break;
//      
//    case StandardApplicationCommandIDs::paste:
//      break;
//      
//    case StandardApplicationCommandIDs::del:
//      break;
//      
//    case StandardApplicationCommandIDs::selectAll:
//      break;
//      
//    case StandardApplicationCommandIDs::deselectAll:
//      break; 
      
    default:
      break;
  }
}

bool MainHostWindow::perform (const InvocationInfo& info)
{
  GraphDocumentComponent* const graphEditor = getGraphEditor();

  switch (info.commandID)
  {
    case CommandIDs::open:
      if (graphEditor != nullptr && graphEditor->graph.saveIfNeededAndUserAgrees() == FileBasedDocument::savedOk)
        graphEditor->graph.loadFromUserSpecifiedFile (true);

      break;

    case CommandIDs::save:
      if (graphEditor != nullptr)
        graphEditor->graph.save (true, true);
      break;

    case CommandIDs::saveAs:
      if (graphEditor != nullptr)
        graphEditor->graph.saveAs (File::nonexistent, true, true, true);
      break;

    case CommandIDs::showPluginListEditor:
      if (pluginListWindow == nullptr)
        pluginListWindow = new PluginListWindow (*this, formatManager);

      pluginListWindow->toFront (true);
      break;

    case CommandIDs::showAudioSettings:
      showAudioSettings();
      break;

    case CommandIDs::aboutBox:
      // TODO
      break;
      
    case CommandIDs::copy:
      // TODO
      break;

    case CommandIDs::paste:
      // TODO
      break;
      
    case CommandIDs::undo:
      // TODO
      graphEditor->undoManager.undo();
      break;
      
    case CommandIDs::redo:
      // TODO
      graphEditor->undoManager.redo();
      break;
      
    case CommandIDs::zoomIn:      getGraphEditor()->setZoom (snapToIntegerZoom (getGraphEditor()->getZoom() * 2.0)); break;
    case CommandIDs::zoomOut:     getGraphEditor()->setZoom (snapToIntegerZoom (getGraphEditor()->getZoom() / 2.0)); break;
    case CommandIDs::zoomNormal:  getGraphEditor()->setZoom (1.0); break;
      
    default:
      return false;
  }

  return true;
}

void MainHostWindow::showAudioSettings()
{
  AudioDeviceSelectorComponent audioSettingsComp (deviceManager,
      0, 256,
      0, 256,
      true, true, true, false);

  audioSettingsComp.setSize (500, 450);

  DialogWindow::LaunchOptions o;
  o.content.setNonOwned (&audioSettingsComp);
  o.dialogTitle                   = "Audio Settings";
  o.componentToCentreAround       = this;
  o.dialogBackgroundColour        = Colours::grey;
  o.escapeKeyTriggersCloseButton  = true;
  o.useNativeTitleBar             = true;
  o.resizable                     = false;

  o.runModal();

  ScopedPointer<XmlElement> audioState (deviceManager.createStateXml());

  getAppProperties().getUserSettings()->setValue ("audioDeviceState", audioState);
  getAppProperties().getUserSettings()->saveIfNeeded();

  GraphDocumentComponent* const graphEditor = getGraphEditor();

  if (graphEditor != nullptr)
    graphEditor->graph.removeIllegalConnections();
}

bool MainHostWindow::isInterestedInFileDrag (const StringArray&)
{
  return true;
}

void MainHostWindow::fileDragEnter (const StringArray&, int, int)
{
}

void MainHostWindow::fileDragMove (const StringArray&, int, int)
{
}

void MainHostWindow::fileDragExit (const StringArray&)
{
}

void MainHostWindow::filesDropped (const StringArray& files, int x, int y)
{
  GraphDocumentComponent* const graphEditor = getGraphEditor();

  if (graphEditor != nullptr)
  {
    if (files.size() == 1 && File (files[0]).hasFileExtension (filenameSuffix))
    {
      if (graphEditor->graph.saveIfNeededAndUserAgrees() == FileBasedDocument::savedOk)
        graphEditor->graph.loadFrom (File (files[0]), true);
    }
    else
    {
      OwnedArray <PluginDescription> typesFound;
      knownPluginList.scanAndAddDragAndDroppedFiles (formatManager, files, typesFound);

      Point<int> pos (graphEditor->getLocalPoint (this, Point<int> (x, y)));

      for (int i = 0; i < jmin (5, typesFound.size()); ++i)
        createPlugin (typesFound.getUnchecked(i), pos.getX(), pos.getY());
    }
  }
}

GraphDocumentComponent* MainHostWindow::getGraphEditor() const
{
  return dynamic_cast <GraphDocumentComponent*> (getContentComponent());
}
