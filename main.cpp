#include "pitchtraining.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    PitchTraining w;
    w.show();
    return a.exec();
}
