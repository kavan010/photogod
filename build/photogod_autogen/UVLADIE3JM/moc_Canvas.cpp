/****************************************************************************
** Meta object code from reading C++ file 'Canvas.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/Canvas.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Canvas.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN6CanvasE_t {};
} // unnamed namespace

template <> constexpr inline auto Canvas::qt_create_metaobjectdata<qt_meta_tag_ZN6CanvasE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "Canvas",
        "colorPicked",
        "",
        "QColor",
        "c",
        "zoomChanged",
        "z",
        "cursorMoved",
        "QPointF",
        "docPos",
        "statusMessage",
        "msg"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'colorPicked'
        QtMocHelpers::SignalData<void(const QColor &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'zoomChanged'
        QtMocHelpers::SignalData<void(double)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 6 },
        }}),
        // Signal 'cursorMoved'
        QtMocHelpers::SignalData<void(const QPointF &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Signal 'statusMessage'
        QtMocHelpers::SignalData<void(const QString &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<Canvas, qt_meta_tag_ZN6CanvasE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject Canvas::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6CanvasE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6CanvasE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN6CanvasE_t>.metaTypes,
    nullptr
} };

void Canvas::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Canvas *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->colorPicked((*reinterpret_cast<std::add_pointer_t<QColor>>(_a[1]))); break;
        case 1: _t->zoomChanged((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 2: _t->cursorMoved((*reinterpret_cast<std::add_pointer_t<QPointF>>(_a[1]))); break;
        case 3: _t->statusMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (Canvas::*)(const QColor & )>(_a, &Canvas::colorPicked, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (Canvas::*)(double )>(_a, &Canvas::zoomChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (Canvas::*)(const QPointF & )>(_a, &Canvas::cursorMoved, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (Canvas::*)(const QString & )>(_a, &Canvas::statusMessage, 3))
            return;
    }
}

const QMetaObject *Canvas::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Canvas::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6CanvasE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int Canvas::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void Canvas::colorPicked(const QColor & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void Canvas::zoomChanged(double _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void Canvas::cursorMoved(const QPointF & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void Canvas::statusMessage(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}
QT_WARNING_POP
