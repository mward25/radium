/****************************************************************************
** ui.h extension file, included from the uic-generated form implementation.
**
** If you want to add, delete, or rename functions or slots, use
** Qt Designer to update this file, preserving your code.
**
** You should not define a constructor or destructor in this file.
** Instead, write your code in functions called init() and destroy().
** These will automatically be called by the form's constructor and
** destructor.
*****************************************************************************/

#include <qtabbar.h>

// Volume

void Instrument_widget::volume_slider_valueChanged( int val)
{
    volume_spin->setValue(127-val);
}


void Instrument_widget::volume_spin_valueChanged( int val )
{
    if( volume_slider->value() != 127-val)
	volume_slider->setValue(127-val);
    fprintf(stderr,"Hepp hepp2 %d\n",val);
    
    patchdata->volume = val;
}


void Instrument_widget::volume_onoff_stateChanged( int val)
{
    if (val==QButton::On){
	volume_slider->setEnabled(true);
	volume_spin->setEnabled(true);;
	 patchdata->volumeonoff = true;
    }else if(val==QButton::Off){
	volume_slider->setEnabled(false);
	volume_spin->setEnabled(false);
	patchdata->volumeonoff = false;	
    }

}


// Velocity

void Instrument_widget::velocity_slider_valueChanged( int val)
{
    velocity_spin->setValue(127-val);
}


void Instrument_widget::velocity_spin_valueChanged( int val)
{
    if( velocity_slider->value() != 127-val)
	velocity_slider->setValue(127-val);
    fprintf(stderr,"Hepp hepp3 %d\n",val);

    g_currpatch->standardvel = val;
}


void Instrument_widget::velocity_onoff_stateChanged( int val)
{
    if (val==QButton::On){
	velocity_slider->setEnabled(true);
	velocity_spin->setEnabled(true);;
    }else if(val==QButton::Off){
	velocity_slider->setEnabled(false);
	velocity_spin->setEnabled(false);
    }
    
    
    fprintf(stderr,"state changed %d\n",val);
}


// Panning

void Instrument_widget::panning_slider_valueChanged( int val)
{
    panning_spin->setValue(val);
}

void Instrument_widget::panning_spin_valueChanged( int val)
{
    if( panning_slider->value() != val)
	panning_slider->setValue(val);
    fprintf(stderr,"Pan %d\n",val);
    
    patchdata->pan = val + 63;
}

void Instrument_widget::panning_onoff_stateChanged( int val)
{
    if (val==QButton::On){
	panning_slider->setEnabled(true);
	panning_spin->setEnabled(true);;
	patchdata->panonoff = true;
    }else if(val==QButton::Off){
	panning_slider->setEnabled(false);
	panning_spin->setEnabled(false);
	patchdata->panonoff = false;	
    }
}



void Instrument_widget::channel_valueChanged( int val)
{
    patchdata->channel = val;
}


void Instrument_widget::msb_valueChanged( int val)
{
    patchdata->MSB = val;
}


void Instrument_widget::lsb_valueChanged( int val)
{
    patchdata->LSB = val;
}



// The returnPressed signal might do exactly what we need. This one is called during instrument widget init as well.
void Instrument_widget::name_widget_textChanged( const QString &string )
{
  //QTabBar *tab_bar = instruments_widget->tabs->tabBar();
  //tab_bar->tab(tab_bar->currentTab())->setText(string);
}




void Instrument_widget::name_widget_returnPressed()
{
    printf("return pressed\n");
    //focus = false;
    
     //QTabBar *tab_bar = instruments_widget->tabs->tabBar();
     //tab_bar->tab(tab_bar->currentTab())->setText(name_widget->text());
    instruments_widget->tabs->setTabLabel(current_instrument, name_widget->text());
    
    set_editor_focus();
}
