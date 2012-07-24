/* Copyright 2012 Kjetil S. Matheussen

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. */


#include "../common/nsmtracker.h"
#include "../common/visual_proc.h"
#include "../common/OS_Bs_edit_proc.h"
#include "../common/blocklist_proc.h"
#include "../api/api_common_proc.h"
#include "../common/player_proc.h"
#include "../common/wblocks_proc.h"
#include "../common/gfx_proc.h"
#include "../common/settings_proc.h"

#include "EditorWidget.h"
#include "Qt_colors_proc.h"

#include "qwidget.h"
#include "qpushbutton.h"
#include "qmainwindow.h"
#include "qsplitter.h"

#ifdef USE_QT3
#include "qlistbox.h"
#endif

#ifdef USE_QT4
#include <QListWidget>
class QListBox : public QListWidget{
public:
  QListBox(QWidget *parent) : QListWidget(parent) {}
  void insertItem(const char *text){
    QListWidget::addItem(text);
  }
  int currentItem(){
    return QListWidget::currentRow();
  }
  void setSelected(int pos, bool something){
    QListWidget::setCurrentRow(pos);
  }
  void removeItem(int pos){
    QListWidget::takeItem(pos);
  }
};

#define QValueList QList
#endif



extern struct Root *root;


static const int xborder = 0;
static const int yborder = 0;

static int button_height = 30;
static int button_width = 50;

static int get_blocklist_x2(bool stacked, int width, int height);

// Returns true if the playlist should be placed above the blocklist.
static bool do_stack(int width, int height){
  return get_blocklist_x2(false, width, height) < 150;
}

static int get_blocklist_x1(bool stacked, int width, int height){
  return xborder;
}

static int get_blocklist_y1(bool stacked, int width, int height){
  return yborder;
}

static int get_add_button_y1(bool stacked, int width, int height);
static int get_blocklist_y2(bool stacked, int width, int height){
  if(stacked)
    return get_add_button_y1(stacked,width,height) - yborder;
  else
    return height;//-yborder;
}

static int get_blocklist_x2(bool stacked, int width, int height){
  if(stacked)
    return width-xborder;
  else
    return width/2 - button_width/2 - xborder;
}

static int get_playlist_x1(bool stacked, int width, int height){
  if(stacked)
    return xborder;
  else
    return width/2 + button_width/2 + xborder;
}

static int get_add_button_y2(bool stacked, int width, int height);
static int get_playlist_y1(bool stacked, int width, int height){
  if(stacked)
    return get_add_button_y2(true,width,height) + xborder;
  else
    return yborder;
}

static int get_playlist_x2(bool stacked, int width, int height){
  return width-xborder;
}

static int get_playlist_y2(bool stacked, int width, int height){
  return height;//-yborder;
}

static int get_add_button_x1(bool stacked, int width, int height){
  if(stacked)
    return xborder; //width/2 - button_width - xborder;
  else
    return width/2 - button_width/2;
}

static int get_add_button_y1(bool stacked, int width, int height){
  if(stacked)
    return height/2 - button_height/2;
  else
    return height/2 - button_height - yborder/2;
}

static int get_add_button_x2(bool stacked, int width, int height){
  if(stacked)
    return width/2 - xborder/2;
  else
    return get_add_button_x1(stacked,width,height) + button_width;
}

static int get_add_button_y2(bool stacked, int width, int height){
  return get_add_button_y1(stacked,width,height) + button_height;
}

static int get_remove_button_x1(bool stacked, int width, int height){
  if(stacked)
    return width/2 + xborder/2;
  else
    return get_add_button_x1(stacked,width,height);
}

static int get_remove_button_y1(bool stacked, int width, int height){
  if(stacked)
    return get_add_button_y1(stacked,width,height);
  else
    return get_add_button_y1(stacked,width,height) + button_height + yborder;
}

static int get_remove_button_x2(bool stacked, int width, int height){
  if(stacked)
    return width-xborder;
  else
    return get_remove_button_x1(stacked,width,height) + button_width;
}

static int get_remove_button_y2(bool stacked, int width, int height){
  return get_remove_button_y1(stacked,width,height) + button_height;
}

static int num_visitors;

struct ScopedVisitors{
  ScopedVisitors(){
    num_visitors++;
  }
  ~ScopedVisitors(){
    num_visitors--;
  }
};

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

#define MOVE_WIDGET(widget) do{ \
  int x1=get_##widget##_x1(stacked,width,height); \
  int y1=get_##widget##_y1(stacked,width,height); \
  int x2=get_##widget##_x2(stacked,width,height); \
  int y2=get_##widget##_y2(stacked,width,height); \
  widget.move(MAX(xborder,x1),MAX(yborder,y1));    \
  widget.setFixedSize(MIN(x2-x1,width-xborder),MIN(y2-y1,height-yborder)); \
  }while(0);

class BlockSelector : public QWidget
{
  Q_OBJECT

public:
  BlockSelector(QWidget *parent)
    : QWidget(parent, (const char*)"Block selector", Qt::WType_Dialog)
    , add_button("->",this)
    , remove_button("<-",this)
    , playlist(this)
    , blocklist(this)
    , last_shown_width(0) // frustrating: SETTINGS_read_int((char*)"blocklist_width",0))
  {
    button_width = add_button.width();
    button_height = add_button.height();

    blocklist.show();
    playlist.show();
    add_button.show();
    remove_button.show();

    playlist.insertItem(" "); // Make it possible to put a block at the end of the playlist.

    resizeEvent(NULL);

    connect(&blocklist, SIGNAL(highlighted(int)), this, SLOT(blocklist_highlighted(int)));
    connect(&blocklist, SIGNAL(selected(int)), this, SLOT(blocklist_selected(int)));
    connect(&playlist, SIGNAL(highlighted(int)), this, SLOT(playlist_highlighted(int)));
    connect(&playlist, SIGNAL(selected(int)), this, SLOT(playlist_selected(int)));

    connect(&add_button, SIGNAL(pressed()), this, SLOT(add_to_playlist()));
    connect(&remove_button, SIGNAL(pressed()), this, SLOT(remove_from_playlist()));

    setWidgetColors(this);
  }

  void resizeEvent(QResizeEvent *qresizeevent){
    //fprintf(stderr,"I am resized\n");

    int width = this->width();
    int height = this->height();
    bool stacked = do_stack(width, height);
    
    MOVE_WIDGET(blocklist);
    MOVE_WIDGET(playlist);
    MOVE_WIDGET(add_button);
    MOVE_WIDGET(remove_button);
  }

  QPushButton add_button;
  QPushButton remove_button;
  QListBox playlist;
  QListBox blocklist;

  int last_shown_width;

private slots:

  void add_to_playlist(void){
#ifdef USE_QT3
    int pos = playlist.currentItem();
#else
    int pos = playlist.currentRow();
#endif
    if(pos==-1)
      return;

    BL_insertCurrPos(playlist.currentItem(), getBlockFromNum(blocklist.currentItem()));

    playlist.setSelected(pos+1, true);
    if(pos<(int)playlist.count()-1)
      root->curr_playlist = pos;
  }

  void remove_from_playlist(void){
    int num = playlist.currentItem();
    if(num==-1)
      return;
    if(playlist.count()<=2)
      return;

    BL_deleteCurrPos(num);

    printf("remove from playlist\n");
  }

  void blocklist_highlighted(int num){
    if(num==-1)
      return;
    printf("num: %d\n",num);

    if(num_visitors>0) // event created internally
      return;

    struct Tracker_Windows *window=getWindowFromNum(-1);
    struct WBlocks *wblock=getWBlockFromNum(-1,num);
    DO_GFX(SelectWBlock(window,wblock));
    EditorWidget *editor = static_cast<EditorWidget*>(root->song->tracker_windows->os_visual.widget);
    editor->update();
  }
  void blocklist_selected(int num){
    if(num==-1)
      return;
    add_to_playlist();
  }

  void playlist_highlighted(int num){
    if(num==-1)
      return;
    if(num==(int)playlist.count()-1)
      return;

    if(num_visitors>0) // event created internally
      return;

    PlayStop();
    root->curr_playlist = num;
  }

  void playlist_selected(int num){
    remove_from_playlist();
  }
};


static BlockSelector *bs;

QWidget *create_blockselector(){
  ScopedVisitors v;

  bs = new BlockSelector (NULL);
  return bs;
}

void BS_resizewindow(void){
  ScopedVisitors v;
}

void BS_UpdateBlockList(void){
  ScopedVisitors v;

  int num_blocks=root->song->num_blocks;
  struct Blocks *block=root->song->blocks;
  int lokke;

  while(bs->blocklist.count()>0)
    bs->blocklist.removeItem(0);

  for(lokke=0;lokke<num_blocks;lokke++){
    char temp[20];

    sprintf(temp,"%d: %s",lokke,block->name);

    bs->blocklist.insertItem(temp);

    block=NextBlock(block);
  }

  //PyRun_SimpleString(temp);

  BS_SelectBlock(root->song->tracker_windows->wblock->block);  
}

void BS_UpdatePlayList(void){
  ScopedVisitors v;

  int pos = bs->playlist.currentItem();//root->curr_playlist;

  while(bs->playlist.count()>0)
    bs->playlist.removeItem(0);

  int lokke=0;
  struct Blocks *block=BL_GetBlockFromPos(lokke);

  while(block!=NULL){
    char temp[500];
    sprintf(temp,"%d: %d/%s",lokke,block->l.num,block->name);

    bs->playlist.insertItem(temp);

    lokke++;
    block=BL_GetBlockFromPos(lokke);
  }

  bs->playlist.insertItem(" "); // Make it possible to put a block at the end of the playlist.

  BS_SelectPlaylistPos(pos);
}

void BS_SelectBlock(struct Blocks *block){
  ScopedVisitors v;
  bs->blocklist.setSelected(block->l.num, true);
}

void BS_SelectPlaylistPos(int pos){
  ScopedVisitors v;
  //printf("selectplaylistpos %d\n",pos);
  if(pos==-1)
    return;
  bs->playlist.setSelected(pos, true);
}

static void set_widget_width(int width){
  QMainWindow *main_window = static_cast<QMainWindow*>(root->song->tracker_windows->os_visual.main_window);
  EditorWidget *editor = static_cast<EditorWidget*>(root->song->tracker_windows->os_visual.widget);
  QSplitter *splitter = editor->xsplitter;

  QValueList<int> currentSizes = splitter->sizes();
  bs->last_shown_width = currentSizes[1];
  currentSizes[0] = main_window->width()-width;
  currentSizes[1] = width;
  splitter->setSizes(currentSizes);
}

void GFX_PlayListWindowToFront(void){
  ScopedVisitors v;

  set_widget_width(bs->last_shown_width > 30 ? bs->last_shown_width : 200);
}

void GFX_PlayListWindowToBack(void){
  ScopedVisitors v;

#if 0
  // Perhaps a value in percentage of screen would work. But the default size (200) is best anyway.
  if(bs->width() > 30)
    SETTINGS_write_int((char*)"blocklist_width",bs->width());
#endif

  set_widget_width(0);
}

void GFX_showHidePlaylist(struct Tracker_Windows *window){
  if(bs->width() < 10)
    GFX_PlayListWindowToFront();
  else
    GFX_PlayListWindowToBack();
}

#include "mQt_Bs_edit.cpp"
