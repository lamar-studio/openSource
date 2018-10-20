/***
  This file is part of pavucontrol.

  Copyright 2006-2008 Lennart Poettering
  Copyright 2009 Colin Guthrie

  pavucontrol is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  pavucontrol is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with pavucontrol. If not, see <http://www.gnu.org/licenses/>.
***/

#ifndef streamwidget_h
#define streamwidget_h

#include "pavucontrol.h"

#include "minimalstreamwidget.h"

class MainWindow;
class ChannelWidget;

class StreamWidget : public MinimalStreamWidget {
public:
    StreamWidget(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& x);
    void init(MainWindow* mainWindow);

    void setChannelMap(const pa_channel_map &m, bool can_decibel);
    void setVolume(const pa_cvolume &volume, bool force = false);
    virtual void updateChannelVolume(int channel, pa_volume_t v);

    void hideLockedChannels(bool hide = true);

    Gtk::ToggleButton *lockToggleButton, *muteToggleButton;
    Gtk::Label *directionLabel;
    Gtk::Button *deviceButton;

    pa_channel_map channelMap;
    pa_cvolume volume;
    pa_stream *peak;

    ChannelWidget *channelWidgets[PA_CHANNELS_MAX];

    virtual void onMuteToggleButton();
    virtual void onLockToggleButton();
    virtual void onDeviceChangePopup();
    virtual bool onContextTriggerEvent(GdkEventButton*);

    sigc::connection timeoutConnection;

    bool timeoutEvent();

    virtual void executeVolumeUpdate();
    virtual void onKill();

protected:
    MainWindow* mpMainWindow;

    Gtk::Menu contextMenu;
    Gtk::MenuItem terminate;
};

#endif
