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
struct qt_meta_stringdata_SearchComboBox_t {
    QByteArrayData data[4];
    char stringdata0[44];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SearchComboBox_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SearchComboBox_t qt_meta_stringdata_SearchComboBox = {
    {
QT_MOC_LITERAL(0, 0, 14), // "SearchComboBox"
QT_MOC_LITERAL(1, 15, 16), // "aboutToShowPopup"
QT_MOC_LITERAL(2, 32, 0), // ""
QT_MOC_LITERAL(3, 33, 10) // "popupShown"

    },
    "SearchComboBox\0aboutToShowPopup\0\0"
    "popupShown"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SearchComboBox[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       2,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   24,    2, 0x06 /* Public */,
       3,    0,   25,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void SearchComboBox::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SearchComboBox *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->aboutToShowPopup(); break;
        case 1: _t->popupShown(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (SearchComboBox::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SearchComboBox::aboutToShowPopup)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (SearchComboBox::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SearchComboBox::popupShown)) {
                *result = 1;
                return;
            }
        }
    }
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject SearchComboBox::staticMetaObject = { {
    QMetaObject::SuperData::link<QComboBox::staticMetaObject>(),
    qt_meta_stringdata_SearchComboBox.data,
    qt_meta_data_SearchComboBox,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *SearchComboBox::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SearchComboBox::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SearchComboBox.stringdata0))
        return static_cast<void*>(this);
    return QComboBox::qt_metacast(_clname);
}

int SearchComboBox::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QComboBox::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 2)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void SearchComboBox::aboutToShowPopup()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void SearchComboBox::popupShown()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
struct qt_meta_stringdata_PressAnalyzer_t {
    QByteArrayData data[22];
    char stringdata0[327];
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
QT_MOC_LITERAL(4, 52, 16), // "loadAndMergeLogs"
QT_MOC_LITERAL(5, 69, 19), // "saveEventListToFile"
QT_MOC_LITERAL(6, 89, 11), // "clearWindow"
QT_MOC_LITERAL(7, 101, 14), // "addEventToList"
QT_MOC_LITERAL(8, 116, 12), // "triggerCount"
QT_MOC_LITERAL(9, 129, 10), // "lineNumber"
QT_MOC_LITERAL(10, 140, 7), // "display"
QT_MOC_LITERAL(11, 148, 14), // "onEventClicked"
QT_MOC_LITERAL(12, 163, 16), // "QListWidgetItem*"
QT_MOC_LITERAL(13, 180, 4), // "item"
QT_MOC_LITERAL(14, 185, 9), // "searchAll"
QT_MOC_LITERAL(15, 195, 14), // "goToPrevSearch"
QT_MOC_LITERAL(16, 210, 14), // "goToNextSearch"
QT_MOC_LITERAL(17, 225, 24), // "onSearchResultRowClicked"
QT_MOC_LITERAL(18, 250, 3), // "row"
QT_MOC_LITERAL(19, 254, 30), // "onSearchResultRowDoubleClicked"
QT_MOC_LITERAL(20, 285, 20), // "onCameraEventClicked"
QT_MOC_LITERAL(21, 306, 20) // "onStatusEventClicked"

    },
    "PressAnalyzer\0loadAndAnalyzeLog\0\0"
    "loadAndAnalyzeLogs\0loadAndMergeLogs\0"
    "saveEventListToFile\0clearWindow\0"
    "addEventToList\0triggerCount\0lineNumber\0"
    "display\0onEventClicked\0QListWidgetItem*\0"
    "item\0searchAll\0goToPrevSearch\0"
    "goToNextSearch\0onSearchResultRowClicked\0"
    "row\0onSearchResultRowDoubleClicked\0"
    "onCameraEventClicked\0onStatusEventClicked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_PressAnalyzer[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   84,    2, 0x08 /* Private */,
       3,    0,   85,    2, 0x08 /* Private */,
       4,    0,   86,    2, 0x08 /* Private */,
       5,    0,   87,    2, 0x08 /* Private */,
       6,    0,   88,    2, 0x08 /* Private */,
       7,    3,   89,    2, 0x08 /* Private */,
      11,    1,   96,    2, 0x08 /* Private */,
      14,    0,   99,    2, 0x08 /* Private */,
      15,    0,  100,    2, 0x08 /* Private */,
      16,    0,  101,    2, 0x08 /* Private */,
      17,    1,  102,    2, 0x08 /* Private */,
      19,    1,  105,    2, 0x08 /* Private */,
      20,    1,  108,    2, 0x08 /* Private */,
      21,    1,  111,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::QString,    8,    9,   10,
    QMetaType::Void, 0x80000000 | 12,   13,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   18,
    QMetaType::Void, QMetaType::Int,   18,
    QMetaType::Void, 0x80000000 | 12,   13,
    QMetaType::Void, 0x80000000 | 12,   13,

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
        case 2: _t->loadAndMergeLogs(); break;
        case 3: _t->saveEventListToFile(); break;
        case 4: _t->clearWindow(); break;
        case 5: _t->addEventToList((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 6: _t->onEventClicked((*reinterpret_cast< QListWidgetItem*(*)>(_a[1]))); break;
        case 7: _t->searchAll(); break;
        case 8: _t->goToPrevSearch(); break;
        case 9: _t->goToNextSearch(); break;
        case 10: _t->onSearchResultRowClicked((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 11: _t->onSearchResultRowDoubleClicked((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 12: _t->onCameraEventClicked((*reinterpret_cast< QListWidgetItem*(*)>(_a[1]))); break;
        case 13: _t->onStatusEventClicked((*reinterpret_cast< QListWidgetItem*(*)>(_a[1]))); break;
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
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 14;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
