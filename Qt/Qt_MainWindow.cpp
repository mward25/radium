/* Copyright 2003 Kjetil S. Matheussen

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


#include <qstatusbar.h>
#include <qmainwindow.h>

#include "EditorWidget.h"

#include "Qt_colors_proc.h"
#include "Qt_Menues_proc.h"
#include "Qt_Fonts_proc.h"

#include "../common/settings_proc.h"

#include "Qt_MainWindow_proc.h"


EditorWidget::EditorWidget(QWidget *parent, const char *name )
  : QFrame( parent, name, Qt::WStaticContents | Qt::WResizeNoErase | Qt::WRepaintNoErase | Qt::WNoAutoErase )
  , qpa(256)
{
  this->qpixmap=NULL;

  setEditorColors(this);

  this->setMouseTracking(true);

  //this->setFrameStyle(QFrame::Raised );
  this->setFrameStyle(QFrame::Sunken );
  this->setFrameShape(QFrame::Panel);
  this->setLineWidth(2);
}

EditorWidget::~EditorWidget()
{
}



EditorWidget *g_editor = NULL;

void SetupMainWindow(void){

  //QMainWindow *main_window = new QMainWindow(NULL, "Radium", Qt::WStyle_Customize | Qt::WStyle_NoBorder);// | Qt::WStyle_Dialog);
  QMainWindow *main_window = new QMainWindow(NULL, "Radium");

#ifdef USE_QT4
  main_window->setAttribute(Qt::WA_PaintOnScreen);
  main_window->setAttribute(Qt::WA_OpaquePaintEvent);
  main_window->setAttribute(Qt::WA_NoSystemBackground);
#endif
  main_window->setBackgroundMode(Qt::NoBackground);

  //QPalette pal = QPalette(main_window->palette());
  //pal.setColor( QPalette::Active, QColorGroup::Background, Qt::green);
  //pal.setColor(main_window->backgroundRole(), Qt::blue);
  //main_window->setPalette(pal);
  //main_window->menuBar()->setPalette(pal);

  EditorWidget *editor=new EditorWidget(main_window,"name");
  editor->setFocus();
#ifdef USE_QT4
  editor->setAttribute(Qt::WA_PaintOnScreen);
  editor->setAttribute(Qt::WA_OpaquePaintEvent);
  editor->setAttribute(Qt::WA_NoSystemBackground);
#endif
  editor->main_window = main_window;

  main_window->setBackgroundMode(Qt::NoBackground);

  main_window->resize(800,400);
  editor->setMinimumWidth(400);
  editor->setMinimumHeight(200);

  main_window->setCaption("Radium editor window");
  main_window->statusBar()->message( "Ready", 2000 );

  {
    QStatusBar *status_bar = main_window->statusBar();
    editor->status_label = new QLabel(status_bar);//"");
    editor->status_label->setFrameStyle(QFrame::Sunken);
    //editor->status_frame->
    status_bar->addWidget(editor->status_label, 1, true);
  }

  initMenues(main_window->menuBar());

  {
    QFont font = QFont("Monospace");

    char *fontstring = SETTINGS_read_string((char*)"font",NULL);
    if(fontstring!=NULL)
      font.fromString(fontstring);

    editor->font = font;

    //editor->font->setStyleHint(QFont::TypeWriter);
    //editor->font->setFixedPitch(false);
  }

  editor->qpixmap=new QPixmap(editor->width(),editor->height());
#ifdef USE_QT3
  editor->qpixmap->setOptimization(QPixmap::BestOptim);
#endif

  editor->cursorpixmap=new QPixmap(editor->width(),editor->height());
#ifdef USE_QT3
  editor->cursorpixmap->setOptimization(QPixmap::BestOptim);
#endif

  g_editor = editor;
}

void GFX_SetMinimumWindowWidth(struct Tracker_Windows *tvisual, int width){
  EditorWidget *editor=(EditorWidget *)tvisual->os_visual.widget;
  editor->setMinimumWidth(width);
}


void GFX_SetWindowTitle(struct Tracker_Windows *tvisual,char *title){
  QMainWindow *main_window = (QMainWindow *)tvisual->os_visual.main_window;
  main_window->setCaption(title);
}

void GFX_SetStatusBar(struct Tracker_Windows *tvisual,char *title){
  //QMainWindow *main_window = (QMainWindow *)tvisual->os_visual.main_window;
  //main_window->statusBar()->message(title);

  EditorWidget *editor=(EditorWidget *)tvisual->os_visual.widget;
  editor->status_label->setText(title);
}


