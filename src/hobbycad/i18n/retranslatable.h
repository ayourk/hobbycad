// SPDX-License-Identifier: GPL-3.0-only
//
// HobbyCAD - ayourk/hobbycad
// src/hobbycad/i18n/retranslatable.h
//
#pragma once

/// A widget that can rebuild its user-visible strings.
///
/// Qt's documented route for live language switching is to reimplement
/// changeEvent() in every class and re-call the property setters there. That
/// works, but it puts a near-identical four-line override in every widget
/// class and nothing notices when a newly added setText() is left out of it.
///
/// This is the same idea with the plumbing hoisted out: implement
/// retranslate() and nothing else. hobbycad::translations::switchTo() walks
/// the widget tree after installing a catalog and calls it on everything that
/// offers it.
///
/// Iterating the widgets like that is usually dismissed, because the
/// retranslate function Qt Designer generates is private and per-class, so a
/// central sweep has nothing it can call. That objection does not apply here:
/// this interface is ours, so the sweep has a public entry point on every
/// participating widget.
///
/// Being pure virtual is the point. A class cannot inherit this and forget to
/// implement it, which is the failure mode a hand-written changeEvent has: the
/// override exists, somebody adds a label a year later, and the label is
/// simply never retranslated. Whether every *string* inside retranslate() is
/// up to date is still a matter of discipline, but the method itself cannot go
/// missing.
///
/// Not a QObject, deliberately. Multiple inheritance from QWidget and a second
/// QObject is not allowed, and moc does not handle a QObject template base; a
/// plain abstract class sidesteps both. Reached by dynamic_cast.
///
/// Qt-free by construction, so it lives beside the GUI rather than in
/// libhobbycad: a wxWidgets front end would want its own equivalent, not this
/// one.
namespace hobbycad {

class Retranslatable
{
public:
    virtual ~Retranslatable() = default;

    /// Re-apply every user-visible string this widget owns, by calling the
    /// same setters the constructor used. Called with a new translator already
    /// installed, so tr() returns the new language.
    ///
    /// Must be safe to call repeatedly, and safe to call before the widget is
    /// shown. It should not rebuild layouts or recreate children: only text.
    ///
    /// Strings composed at runtime ("%1 of %2 entities" and the like) must
    /// be rebuilt from current state here, not restored from a remembered
    /// string: the values may have changed since, and some languages reorder
    /// the arguments.
    virtual void retranslate() = 0;
};

} // namespace hobbycad
