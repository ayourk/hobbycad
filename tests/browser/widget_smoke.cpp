#include <QApplication>
#include <QTreeWidget>
#include <hobbycad/browser.h>
#include <hobbycad/project.h>
#include <TopoDS_Shape.hxx>
#include "objectsbrowserwidget.h"
#include <cstdio>
using namespace hobbycad;
static int fails=0;
static void ck(bool ok,const char*w){std::printf("  [%s] %s\n",ok?"PASS":"FAIL",w);if(!ok)++fails;}
int main(int argc,char**argv){
    QApplication app(argc,argv);
    Project p;
    SketchData s; s.name="Profile"; p.addSketch(s);
    p.addBody(TopoDS_Shape{}); p.addBody(TopoDS_Shape{});
    ObjectsBrowserWidget w;
    BrowserOptions o;   // defaults: empty folders are not shown
    w.setTree(buildBrowserTree(p, o));
    QTreeWidget* t = w.treeWidget();
    ck(t!=nullptr, "widget exposes its tree");
    ck(t->topLevelItemCount()>0, "tree is populated");
    // Nothing holds folder pointers any more, so empty folders are gone:
    // Construction has no planes here and must be absent.
    bool bodies=false, sketches=false, construction=false;
    for(int i=0;i<t->topLevelItemCount();++i){
        const QString l=t->topLevelItem(i)->text(0);
        if(l=="Bodies")bodies=true; if(l=="Sketches")sketches=true; if(l=="Construction")construction=true;
    }
    ck(bodies&&sketches,"folders with content are present");
    ck(!construction,"and an empty folder is not shown at all");

    // A rebuild must not lose what the user was looking at.
    QTreeWidgetItem* sketchesItem=nullptr;
    for(int i=0;i<t->topLevelItemCount();++i)
        if(t->topLevelItem(i)->text(0)=="Sketches") sketchesItem=t->topLevelItem(i);
    ck(sketchesItem!=nullptr,"the Sketches folder is there to expand");
    if(sketchesItem){
        sketchesItem->setExpanded(false);
        t->setCurrentItem(w.itemFor(NodeType::Body,2));
        const ObjectsBrowserWidget::ViewState st = w.viewState();
        w.setTree(buildBrowserTree(p, o));
        w.restoreViewState(st);
        QTreeWidgetItem* again=nullptr;
        for(int i=0;i<t->topLevelItemCount();++i)
            if(t->topLevelItem(i)->text(0)=="Sketches") again=t->topLevelItem(i);
        ck(again && !again->isExpanded(),"a collapsed folder stays collapsed across a rebuild");
        QTreeWidgetItem* cur=t->currentItem();
        ck(cur && cur->data(0,Qt::UserRole+1).toInt()==2
              && (NodeType)cur->data(0,Qt::UserRole+2).toInt()==NodeType::Body,
           "and the selected node is still selected");
    }
    QTreeWidgetItem* xy = w.itemFor(NodeType::OriginPlane, (int)SketchPlane::XY);
    ck(xy!=nullptr,"XY plane is addressable by type+id");
    ck(xy && xy->data(0,Qt::UserRole).toString()=="origin_plane",
       "and carries the legacy tag the handlers compare");
    // Body ids start at 1: 0 means "unassigned", never a real body.
    QTreeWidgetItem* b0 = w.itemFor(NodeType::Body,1);
    ck(b0!=nullptr,"the first body is addressable by its ID, not its position");
    ck(w.itemFor(NodeType::Body,0)==nullptr,"and id 0 matches nothing");
    ck(b0 && (b0->flags()&Qt::ItemIsUserCheckable),"body has a visibility checkbox");
    ck(b0 && b0->checkState(0)==Qt::Checked,"and starts visible");
    QTreeWidgetItem* ax = w.itemFor(NodeType::OriginAxis,0);
    ck(ax && ax->data(0,Qt::UserRole).toString()=="origin_axis","X axis is now tagged");
    // a missing external reference must render with a badge
    BrowserNode root; root.type=NodeType::Root;
    BrowserNode ext; ext.type=NodeType::ProjectExternal; ext.name="Bracket";
    ext.refKind=ReferenceKind::External; ext.refState=ReferenceState::Missing;
    ext.refPath="/gone/bracket"; ext.badge=badgeForReference(ext.refKind,ext.refState);
    ext.flags=NodeRelinkable; root.children.push_back(ext);
    w.setTree(root);
    QTreeWidgetItem* e = w.itemFor(NodeType::ProjectExternal,-1);
    ck(e!=nullptr,"a missing external reference still renders as a node");
    ck(e && !e->icon(0).isNull(),"and carries a visible badge, not a blank row");
    ck(e && e->toolTip(0).contains("/gone/bracket"),"with the path it could not find");
    ck(e && e->toolTip(0).contains("Cannot find"),"and says plainly that it could not be found");
    std::printf("\n%s (%d failure(s))\n",fails?"FAILURES":"ALL PASS",fails);
    return fails?1:0;
}
