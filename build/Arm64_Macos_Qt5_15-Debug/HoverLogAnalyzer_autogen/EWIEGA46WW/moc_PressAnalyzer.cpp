/****************************************************************************
** Meta object code from reading C++ file 'PressAnalyzer.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.10)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../PressAnalyzer.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'PressAnalyzer.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.10. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_PressAnalyzer_t {
    QByteArrayData data[18];
    char stringdata0[251];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_PressAnalyzer_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_PressAnalyzer_t qt_meta_stringdata_PressAnalyzer = {
    {
QT_MOC_LITERAL(0, 0, 13), // "PressAnalyzer"
QT_MOC_LITERAL(1, 14, 17), // "loadAndAnalyzeLog"
QT_MOC_LITERAL(2, 32, 0), // ""
QT_MOC_LITERAL(3, 33, 18), // "loadAndAnalyzeLogs"
QT_MOC_LITERAL(4, 52, 19), // "saveEventListToFile"
QT_MOC_LITERAL(5, 72, 11), // "clearWindow"
QT_MOC_LITERAL(6, 84, 14), // "addEventToList"
QT_MOC_LITERAL(7, 99, 12), // "triggerCount"
QT_MOC_LITERAL(8, 112, 10), // "lineNumber"
QT_MOC_LITERAL(9, 123, 7), // "display"
QT_MOC_LITERAL(10, 131, 14), // "onEventClicked"
QT_MOC_LITERAL(11, 146, 16), // "QListWidgetItem*"
QT_MOC_LITERAL(12, 163, 4), // "item"
QT_MOC_LITERAL(13, 168, 9), // "searchAll"
QT_MOC_LITERAL(14, 178, 14), // "goToPrevSearch"
QT_MOC_LITERAL(15, 193, 14), // "goToNextSearch"
QT_MOC_LITERAL(16, 208, 21), // "onSearchResultClicked"
QT_MOC_LITERAL(17, 230, 20) // "onCameraEventClicked"

    },
    "PressAnalyzer\0loadAndAnalyzeLog\0\0"
    "loadAndAnalyzeLogs\0saveEventListToFile\0"
    "clearWindow\0addEventToList\0triggerCount\0"
    "lineNumber\0display\0onEventClicked\0"
    "QListWidgetItem*\0item\0searchAll\0"
    "goToPrevSearch\0goToNextSearch\0"
    "onSearchResultClicked\0onCameraEventClicked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_PressAnalyzer[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      11,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   69,    2, 0x08 /* Private */,
       3,    0,   70,    2, 0x08 /* Private */,
       4,    0,   71,    2, 0x08 /* Private */,
       5,    0,   72,    2, 0x08 /* Private */,
       6,    3,   73,    2, 0x08 /* Private */,
      10,    1,   80,    2, 0x08 /* Private */,
      13,    0,   83,    2, 0x08 /* Private */,
      14,    0,   84,    2, 0x08 /* Private */,
      15,    0,   85,    2, 0x08 /* Private */,
      16,    1,   86,    2, 0x08 /* Private */,
      17,    1,   89,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::QString,    7,    8,    9,
    QMetaType::Void, 0x80000000 | 11,   12,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 11,   12,
    QMetaType::Void, 0x80000000 | 11,   12,

       0        // eod
};

void PressAnalyzer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<PressAnalyzer *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->loadAndAnalyzeLog(); break;
        case 1: _t->loadAndAnalyzeLogs(); break;
        case 2: _t->saveEventListToFile(); break;
        case 3: _t->clearWindow(); break;
        case 4: _t->addEventToList((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 5: _t->onEventClicked((*reinterpret_cast< QListWidgetItem*(*)>(_a[1]))); break;
        case 6: _t->searchAll(); break;
        case 7: _t->goToPrevSearch(); break;
        case 8: _t->goToNextSearch(); break;
        case 9: _t->onSearchResultClicked((*reinterpret_cast< QListWidgetItem*(*)>(_a[1]))); break;
        case 10: _t->onCameraEventClicked((*reinterpret_cast< QListWidgetItem*(*)>(_a[1]))); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject PressAnalyzer::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_PressAnalyzer.data,
    qt_meta_data_PressAnalyzer,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *PressAnalyzer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PressAnalyzer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_PressAnalyzer.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int PressAnalyzer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 11;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
