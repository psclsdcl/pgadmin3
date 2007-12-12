//////////////////////////////////////////////////////////////////////////
//
// pgAdmin III - PostgreSQL Tools
// RCS-ID:      $Id$
// Copyright (C) 2002 - 2007, The pgAdmin Development Team
// This software is released under the Artistic Licence
//
// dlgQuery.cpp - Property Dialog
//
//////////////////////////////////////////////////////////////////////////

// wxWindows headers
#include <wx/wx.h>
#include <wx/button.h>

// App headers
#include "pgAdmin3.h"
#include "ctl/ctlSQLBox.h"
#include "schema/pgCollection.h"
#include "schema/pgDatatype.h"
#include "utils/misc.h"
#include "utils/pgDefs.h"
#include "ctl/ctlSecurityPanel.h"

// Images
#include "images/properties.xpm"

#include "frm/frmMain.h"
#include "frm/frmHint.h"

// Property dialogs
#include "dlg/dlgProperty.h"
#include "dlg/dlgServer.h"
#include "dlg/dlgAggregate.h"
#include "dlg/dlgColumn.h"
#include "dlg/dlgIndex.h"
#include "dlg/dlgIndexConstraint.h"
#include "dlg/dlgForeignKey.h"
#include "dlg/dlgCheck.h"
#include "dlg/dlgRule.h"
#include "dlg/dlgTrigger.h"
#include "agent/dlgJob.h"
#include "agent/dlgStep.h"
#include "agent/dlgSchedule.h"

#include "slony/dlgRepCluster.h"
#include "slony/dlgRepNode.h"
#include "slony/dlgRepPath.h"
#include "slony/dlgRepListen.h"
#include "slony/dlgRepSet.h"
#include "slony/dlgRepSequence.h"
#include "slony/dlgRepTable.h"
#include "slony/dlgRepSubscription.h"
#include "schema/pgTable.h"
#include "schema/pgColumn.h"
#include "schema/pgTrigger.h"
#include "schema/pgGroup.h"
#include "schema/pgUser.h"




class replClientData : public wxClientData
{
public:
    replClientData(const wxString &c, long s, long ma, long mi) { cluster=c; setId=s; majorVer=ma; minorVer=mi; }
    wxString cluster;
    long setId;
    long majorVer;
    long minorVer;
};


BEGIN_EVENT_TABLE(dlgProperty, DialogWithHelp)
    EVT_NOTEBOOK_PAGE_CHANGED(XRCID("nbNotebook"),  dlgProperty::OnPageSelect)  

    EVT_TEXT(XRCID("txtName"),                      dlgProperty::OnChange)
    EVT_TEXT(XRCID("cbOwner"),                      dlgProperty::OnChangeOwner)
    EVT_COMBOBOX(XRCID("cbOwner"),                  dlgProperty::OnChange)
    EVT_TEXT(XRCID("txtComment"),                   dlgProperty::OnChange)
    
    EVT_BUTTON(wxID_HELP,                           dlgProperty::OnHelp)
    EVT_BUTTON(wxID_OK,                             dlgProperty::OnOK)
    EVT_BUTTON(wxID_APPLY,                          dlgProperty::OnApply)
END_EVENT_TABLE();


dlgProperty::dlgProperty(pgaFactory *f, frmMain *frame, const wxString &resName) : DialogWithHelp(frame)
{
    readOnly=false;
    sqlPane=0;
    processing=false;
    mainForm=frame;
    database=0;
    connection=0;
    factory=f;
    item = (void *)NULL;
    owneritem = (void *)NULL;
    wxWindowBase::SetFont(settings->GetSystemFont());
    LoadResource(frame, resName);

#ifdef __WXMSW__
    SetWindowStyleFlag(GetWindowStyleFlag() & ~wxMAXIMIZE_BOX);
#endif

    nbNotebook = CTRL_NOTEBOOK("nbNotebook");
    if (!nbNotebook)
    {
        wxMessageBox(wxString::Format(_("Problem with resource %s: Notebook not found.\nPrepare to crash!"), resName.c_str()));
        return;
    }
    SetIcon(wxIcon(factory->GetImage()));

    txtName = CTRL_TEXT("txtName");
    txtOid = CTRL_TEXT("txtOID");
    txtComment = CTRL_TEXT("txtComment");
    cbOwner = CTRL_COMBOBOX2("cbOwner");
    cbClusterSet = CTRL_COMBOBOX2("cbClusterSet");

    wxNotebookPage *page=nbNotebook->GetPage(0);
    wxASSERT(page != NULL);
    page->GetClientSize(&width, &height);

    numericValidator.SetStyle(wxFILTER_NUMERIC);
    btnOK->Disable();

    AddStatusBar();
}


dlgProperty::~dlgProperty()
{
    wxString prop=wxT("Properties/") + wxString(factory->GetTypeName());
	settings->Write(prop, GetPosition());

    if (GetWindowStyle() & wxRESIZE_BORDER)
        settings->Write(prop, GetSize());
}


wxString dlgProperty::GetHelpPage() const
{
    wxString page;

    pgObject *obj=((dlgProperty*)this)->GetObject();
    if (obj)
        page=obj->GetHelpPage(false);
    else
    {
        // Attempt to get he page from the dialogue, otherwise, take a shot at it!
        page=this->GetHelpPage(true);
        if (page.Length() == 0)
        {
            page=wxT("pg/sql-create");
            page += wxString(factory->GetTypeName()).Lower();
        }
    }
			
    return page;
}


void dlgProperty::CheckValid(bool &enable, const bool condition, const wxString &msg)
{
    if (enable)
    {
        if (!condition)
        {
            if (statusBar)
                statusBar->SetStatusText(msg);
            enable=false;
        }
    }
}


void dlgProperty::SetDatabase(pgDatabase *db)
{
    database=db;
    if (db)
        connection=db->GetConnection();
}

    
void dlgProperty::EnableOK(bool enable)
{
    wxButton *btn=btnApply;
    if (btn)
        btn->Enable(enable);

    btnOK->Enable(enable);
    if (enable)
    {
        if (statusBar)
            statusBar->SetStatusText(wxEmptyString);
    }
}


int dlgProperty::Go(bool modal)
{
    wxASSERT(factory != 0);

    // restore previous position and size, if applicable
    wxString prop = wxT("Properties/") + wxString(factory->GetTypeName());

    wxSize origSize = GetSize();

    if (GetWindowStyle() & wxRESIZE_BORDER)
        SetSize(settings->Read(prop, GetSize()));

    wxPoint pos=settings->Read(prop, GetPosition());
    if (pos.x >= 0 && pos.y >= 0)
        Move(pos);

    wxSize size = GetSize();
    CheckOnScreen(this, pos, size, origSize.GetWidth(), origSize.GetHeight());
        Move(pos);

    ctlComboBoxFix *cbowner = (ctlComboBoxFix*)cbOwner;

    if (cbClusterSet)
    {
        cbClusterSet->Append(wxEmptyString);
        cbClusterSet->SetSelection(0);

        if (mainForm && database)
        {
            wxArrayString clusters=database->GetSlonyClusters(mainForm->GetBrowser());

            size_t i;
            for (i=0 ; i < clusters.GetCount() ; i++)
            {
                wxString cluster=wxT("_") + clusters.Item(i);
                pgSetIterator sets(connection, 
                    wxT("SELECT set_id, ") + qtIdent(cluster) + wxT(".slonyversionmajor(), ") + qtIdent(cluster) + wxT(".slonyversionminor()\n")
                    wxT("  FROM ") + qtIdent(cluster) + wxT(".sl_set\n")
                    wxT(" WHERE set_origin = ") + qtIdent(cluster) + 
                    wxT(".getlocalnodeid(") + qtDbString(cluster) + wxT(");"));
                
                while (sets.RowsLeft())
                {
                    wxString str;
                    long setId=sets.GetLong(wxT("set_id"));
                    long majorVer=sets.GetLong(wxT("slonyversionmajor"));
                    long minorVer=sets.GetLong(wxT("slonyversionminor"));
                    str.Printf(_("Cluster \"%s\", set %ld"), clusters.Item(i).c_str(), setId);
                    cbClusterSet->Append(str, new replClientData(cluster, setId, majorVer, minorVer));
                }
            }
        }
        if (cbClusterSet->GetCount() < 2)
            cbClusterSet->Disable();
    }

    if (cbowner && !cbowner->GetCount())
    {
        if (!GetObject())
            cbOwner->Append(wxEmptyString);
        AddUsers(cbowner);
    }
    if (txtOid)
        txtOid->Disable();

    if (GetObject())
    {
        if (txtName)
            txtName->SetValue(GetObject()->GetName());
        if (txtOid)
            txtOid->SetValue(NumToStr((unsigned long)GetObject()->GetOid()));
        if (cbOwner)
            cbOwner->SetValue(GetObject()->GetOwner());
        if (txtComment)
            txtComment->SetValue(GetObject()->GetComment());


        if (!readOnly && !GetObject()->CanCreate())
        {
            // users who can't create will usually not be allowed to change either.
            readOnly=false;
        }

        wxString typeName = factory->GetTypeName();
        SetTitle(wxString(wxGetTranslation(typeName)) + wxT(" ") + GetObject()->GetFullIdentifier());
    }
    else
    {
        wxButton *btn=btnApply;
        if (btn)
            btn->Hide();
        if (factory)
            SetTitle(wxGetTranslation(factory->GetNewString()));
    }
    if (statusBar)
        statusBar->SetStatusText(wxEmptyString);

    if (nbNotebook)
    {
        wxNotebookPage *pg=nbNotebook->GetPage(0);
        if (pg)
            pg->SetFocus();
    }
    if (modal)
        return ShowModal();
    else
        Show(true);
    return 0;
}


void dlgProperty::CreateAdditionalPages()
{
    sqlPane = new ctlSQLBox(nbNotebook, CTL_PROPSQL, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxSUNKEN_BORDER | wxTE_READONLY | wxTE_RICH2);
    nbNotebook->AddPage(sqlPane, wxT("SQL"));
}


wxString dlgProperty::GetName()
{
    if (txtName)
        return txtName->GetValue().Strip(wxString::both);
    return wxEmptyString;
}


void dlgProperty::AppendNameChange(wxString &sql, const wxString &objName)
{
    if (GetObject()->GetName() != GetName())
    {
        if (objName.Length() > 0)
        {
            sql += wxT("ALTER ") + objName
                +  wxT(" RENAME TO ") + qtIdent(GetName())
                +  wxT(";\n");
        } else {
            sql += wxT("ALTER ") + GetObject()->GetTypeName().MakeUpper()
                +  wxT(" ") + GetObject()->GetQuotedFullIdentifier()
                +  wxT(" RENAME TO ") + qtIdent(GetName())
                +  wxT(";\n");
        }
    }
}


void dlgProperty::AppendOwnerChange(wxString &sql, const wxString &objName)
{
    if (GetObject()->GetOwner() != cbOwner->GetValue())
    {
        sql += wxT("ALTER ") + objName
            +  wxT(" OWNER TO ") + qtIdent(cbOwner->GetValue()) 
            +  wxT(";\n");
    }
}


void dlgProperty::AppendOwnerNew(wxString &sql, const wxString &objName)
{
    if (cbOwner->GetGuessedSelection() > 0)
        sql += wxT("ALTER ") + objName
            +  wxT(" OWNER TO ") + qtIdent(cbOwner->GetValue())
            +  wxT(";\n");
}


void dlgProperty::AppendComment(wxString &sql, const wxString &objName, pgObject *obj)
{
    wxString comment=txtComment->GetValue();
    if ((!obj && !comment.IsEmpty()) ||(obj && obj->GetComment() != comment))
    {
        sql += wxT("COMMENT ON ") + objName
            + wxT(" IS ") + qtDbString(comment) + wxT(";\n");
    }
}


void dlgProperty::AppendComment(wxString &sql, const wxString &objType, pgSchema *schema, pgObject *obj)
{
    wxString comment=txtComment->GetValue();
    if ((!obj && !comment.IsEmpty()) ||(obj && obj->GetComment() != comment))
    {
        sql += wxT("COMMENT ON ") + objType + wxT(" ");
        if (schema)
           sql += schema->GetQuotedPrefix();
        sql += qtIdent(GetName()) + wxT(" IS ") + qtDbString(comment) + wxT(";\n");
    }
}


void dlgProperty::AppendQuoted(wxString &sql, const wxString &name)
{
    // quick and quite dirty:
    // !!! this is unsafe if the name itself contains a dot which isn't meant as separator between schema and object
    if (name.First('.') >= 0)
    {
        sql += qtIdent(name.BeforeFirst('.')) + wxT(".") + qtIdent(name.AfterFirst('.'));
    }
    else
        sql += qtIdent(name);
}

void dlgProperty::AppendQuotedType(wxString &sql, const wxString &name)
{
	// see AppendQuoted()
    if (name.First('.') >= 0)
    {
        sql += qtIdent(name.BeforeFirst('.')) + wxT(".") + qtTypeIdent(name.AfterFirst('.'));
    }
    else
        sql += qtTypeIdent(name);
}


void dlgProperty::FillCombobox(const wxString &query, ctlComboBoxFix *cb1, ctlComboBoxFix *cb2)
{
    if (!cb1 && !cb2)
        return;

    pgSet *set=connection->ExecuteSet(query);
    if (set)
    {
        while (!set->Eof())
        {
            if (cb1)
                cb1->Append(set->GetVal(0));
            if (cb2)
                cb2->Append(set->GetVal(0));
            set->MoveNext();
        }
    }
}


void dlgProperty::AddUsers(ctlComboBoxFix *cb1, ctlComboBoxFix *cb2)
{
    FillCombobox(wxT("SELECT usename FROM pg_user ORDER BY usename"), cb1, cb2);
}


void dlgProperty::PrepareTablespace(ctlComboBoxFix *cb, const OID current)
{
    wxASSERT(cb != 0);

    if (connection->BackendMinimumVersion(8, 0))
    {
		// Populate the combo
		cb->FillOidKey(connection, wxT("SELECT oid, spcname FROM pg_tablespace WHERE spcname <> 'pg_global' ORDER BY spcname"));

        if (current)
			cb->SetKey(current);
        else
        {
            if (database)
                cb->SetValue(database->GetDefaultTablespace());
            else
            {
                wxString def = connection->ExecuteScalar(wxT("SELECT current_setting('default_tablespace');"));
                if (def == wxEmptyString || def == wxT("unset"))
                    def = wxT("pg_default");
                cb->SetValue(def);
            }
        }
    }
    else
        cb->Disable();
}


void dlgProperty::OnChangeStc(wxStyledTextEvent &ev)
{
    CheckChange();
}


void dlgProperty::OnChange(wxCommandEvent &ev)
{
    CheckChange();
}


void dlgProperty::OnChangeOwner(wxCommandEvent &ev)
{
    ctlComboBox *cb=cbOwner;
    if (cb)
        cb->GuessSelection(ev);
    CheckChange();
}


bool dlgProperty::tryUpdate(wxTreeItemId collectionItem)
{
    ctlTree *browser=mainForm->GetBrowser();
    pgCollection *collection = (pgCollection*)browser->GetObject(collectionItem);
    if (collection && collection->IsCollection() && factory->GetCollectionFactory() == collection->GetFactory())
    {
        pgObject *data = CreateObject(collection);
        if (data)
        {

            wxString nodeName = this->GetDisplayName();
            if (nodeName.IsEmpty())
                nodeName = data->GetDisplayName();

            size_t pos=0;
            wxTreeItemId newItem;

            if (!data->IsCreatedBy(columnFactory))
            {
                // columns should be appended, not inserted alphabetically

                wxCookieType cookie;
                newItem=browser->GetFirstChild(collectionItem, cookie);
                while (newItem)
                {
                    if (browser->GetItemText(newItem) > nodeName)
                        break;
                    pos++;
                    newItem=browser->GetNextChild(collectionItem, cookie);
                }
            }

            if (newItem)
                browser->InsertItem(collectionItem, pos, nodeName, data->GetIconId(), -1, data);
            else    
                browser->AppendItem(collectionItem, nodeName, data->GetIconId(), -1, data);

            if (data->WantDummyChild())
                browser->AppendItem(data->GetId(), wxT("Dummy"));

            if (browser->GetSelection() == item)
                collection->ShowTreeDetail(browser, 0, mainForm->GetProperties());
            else
                collection->UpdateChildCount(browser);
        }
        else
        {
            // CreateObject didn't return a new pgObject; refresh the complete collection
            mainForm->Refresh(collection);
        }
        return true;
    }
    return false;
}



void dlgProperty::ShowObject()
{
    pgObject *data=GetObject();

    // We might have a parent to refresh. If so, the children will
	// inherently get refreshed as well. Yay :-)
    if (owneritem)
    {
        // Stash the selected items path
        wxString currentPath = mainForm->GetCurrentNodePath();

        pgObject *tblobj = mainForm->GetBrowser()->GetObject(owneritem); 

        if (tblobj) 
            mainForm->Refresh(tblobj);

        // Restore the previous selection...
		mainForm->SetCurrentNode(mainForm->GetBrowser()->GetRootItem(), currentPath);
    }
	else if (data)
    {
        pgObject *newData=data->Refresh(mainForm->GetBrowser(), item);
        if (newData && newData != data)
        {
            mainForm->SetCurrentObject(newData);
            mainForm->GetBrowser()->SetItemData(item, newData);

            newData->SetId(item);
            delete data;
            SetObject(newData);

            newData->UpdateIcon(mainForm->GetBrowser());
        }
        if (newData)
        {
            mainForm->GetBrowser()->DeleteChildren(newData->GetId());

            if (item == mainForm->GetBrowser()->GetSelection())
                newData->ShowTree(mainForm, mainForm->GetBrowser(), mainForm->GetProperties(), 0);
            mainForm->GetBrowser()->SetItemText(item, newData->GetFullName());
            mainForm->GetSqlPane()->SetReadOnly(false);
            mainForm->GetSqlPane()->SetText(newData->GetSql(mainForm->GetBrowser()));
            mainForm->GetSqlPane()->SetReadOnly(true);
        }
    }
    else if (item)
    {
        wxTreeItemId collectionItem=item;

        while (collectionItem)
        {
            // search up the tree for our collection
            if (tryUpdate(collectionItem))
                break;
            collectionItem=mainForm->GetBrowser()->GetItemParent(collectionItem);
        }
    }
    else // Brute force update the current item
    {
        pgObject *currobj = mainForm->GetBrowser()->GetObject(mainForm->GetBrowser()->GetSelection()); 

        if (currobj) 
            mainForm->Refresh(currobj);
    }
}


bool dlgProperty::apply(const wxString &sql, const wxString &sql2)
{
    if (!sql.IsEmpty())
    {
        wxString tmp;
        if (cbClusterSet && cbClusterSet->GetSelection() > 0)
        {
            replClientData *data=(replClientData*)cbClusterSet->GetClientData(cbClusterSet->GetSelection());

            if (data->majorVer > 1 || (data->majorVer == 1 && data->minorVer >= 2))
            {
                tmp = wxT("SELECT ") + qtIdent(data->cluster)
                    + wxT(".ddlscript_prepare(") + NumToStr(data->setId) + wxT(", 0);\n")
                    + wxT("SELECT ") + qtIdent(data->cluster)
                    + wxT(".ddlscript_complete(") + NumToStr(data->setId) + wxT(", ")
                    + qtDbString(sql) + wxT(", 0);\n");
            }
            else
            {
                tmp = wxT("SELECT ") + qtIdent(data->cluster)
                    + wxT(".ddlscript(") + NumToStr(data->setId) + wxT(", ")
                    + qtDbString(sql) + wxT(", 0);\n");
            }
        }
        else
            tmp = sql;

        if (!connection->ExecuteVoid(tmp))
        {
            // error message is displayed inside ExecuteVoid
            return false;
        }

        if (database)
            database->AppendSchemaChange(tmp);
    }

    // Process the second SQL statement. This is primarily only used by
    // CREATE DATABASE which cannot be run in a multi-statement query in 
    // PostgreSQL 8.3+
    if (!sql2.IsEmpty())
    {
        wxString tmp;
        if (cbClusterSet && cbClusterSet->GetSelection() > 0)
        {
            replClientData *data=(replClientData*)cbClusterSet->GetClientData(cbClusterSet->GetSelection());

            if (data->majorVer > 1 || (data->majorVer == 1 && data->minorVer >= 2))
            {
                tmp = wxT("SELECT ") + qtIdent(data->cluster)
                    + wxT(".ddlscript_prepare(") + NumToStr(data->setId) + wxT(", 0);\n")
                    + wxT("SELECT ") + qtIdent(data->cluster)
                    + wxT(".ddlscript_complete(") + NumToStr(data->setId) + wxT(", ")
                    + qtDbString(sql2) + wxT(", 0);\n");
            }
            else
            {
                tmp = wxT("SELECT ") + qtIdent(data->cluster)
                    + wxT(".ddlscript(") + NumToStr(data->setId) + wxT(", ")
                    + qtDbString(sql2) + wxT(", 0);\n");
            }
        }
        else
            tmp = sql2;

        if (!connection->ExecuteVoid(tmp))
        {
            // error message is displayed inside ExecuteVoid
            // Warn the user about partially applied changes, but don't bail out. 
            // Carry on as if everything was successful (because the most important
            // change was!!
            wxMessageBox(_("An error occured executing the second stage SQL statement.\n\nChanges may have been partially applied."), _("Warning"), wxICON_EXCLAMATION | wxOK, this);
        }
        else // Only apend schema changes if there was no error.
        {
            if (database)
                database->AppendSchemaChange(tmp);
        }
    }

    ShowObject();

    return true;
}


void dlgProperty::OnApply(wxCommandEvent &ev)
{
	if (!IsUpToDate())
	{
		if (wxMessageBox(wxT("The object has been changed by another user. Do you wish to continue to to try to update it?"), wxT("Overwrite changes?"), wxYES_NO) == wxNO)
			return;
	}

    EnableOK(false);

    wxString sql=GetSql();
    wxString sql2=GetSql2();

    if (!apply(sql, sql2))
        return;

    if (statusBar)
        statusBar->SetStatusText(_("Changes applied."));
}


void dlgProperty::OnOK(wxCommandEvent &ev)
{
	if (!IsUpToDate())
	{
		if (wxMessageBox(wxT("The object has been changed by another user. Do you wish to continue to to try to update it?"), wxT("Overwrite changes?"), wxYES_NO) == wxNO)
			return;
	}

    EnableOK(false);

    if (IsModal())
    {
        EndModal(0);
        return;
    }

    wxString sql=GetSql();
    wxString sql2=GetSql2();

    if (!apply(sql, sql2))
    {
        EnableOK(true);
        return;
    }

    Destroy();
}


void dlgProperty::OnPageSelect(wxNotebookEvent& event)
{
    if (sqlPane && event.GetSelection() == (int)nbNotebook->GetPageCount()-1)
    {
        sqlPane->SetReadOnly(false);
        if (btnOK->IsEnabled())
        {
            wxString tmp;
            if (cbClusterSet && cbClusterSet->GetSelection() > 0)
            {
                replClientData *data=(replClientData*)cbClusterSet->GetClientData(cbClusterSet->GetSelection());
                tmp.Printf(_("-- Execute replicated using cluster \"%s\", set %ld\n"), data->cluster.c_str(), data->setId);
            }
            sqlPane->SetText(tmp + GetSql() + GetSql2());
        }
        else
        {
            if (GetObject())
                sqlPane->SetText(_("-- nothing to change"));
            else
                sqlPane->SetText(_("-- definition incomplete"));
        }
        sqlPane->SetReadOnly(true);
    }
}



void dlgProperty::InitDialog(frmMain *frame, pgObject *node)
{
    CenterOnParent();
    if (!connection)
        connection=node->GetConnection();
    database=node->GetDatabase();

    if (factory != node->GetFactory() && !node->IsCollection())
    {
        wxCookieType cookie;
        wxTreeItemId collectionItem=frame->GetBrowser()->GetFirstChild(node->GetId(), cookie);
        while (collectionItem)
        {
            pgCollection *collection=(pgCollection*)frame->GetBrowser()->GetObject(collectionItem);
            if (collection && collection->IsCollection() && collection->IsCollectionFor(node))
                break;

            collectionItem=frame->GetBrowser()->GetNextChild(node->GetId(), cookie);
        }
        item=collectionItem;
    }
    else
        item=node->GetId();

    // Additional hacks to get the table to refresh when modifying sub-objects
    if (!item && (node->GetMetaType() == PGM_TABLE || node->GetMetaType() == PGM_VIEW))
        owneritem=node->GetId();

	int metatype = node->GetMetaType();

	switch (metatype)
	{
		case PGM_CHECK:
		case PGM_COLUMN: 
		case PGM_CONSTRAINT:
		case PGM_FOREIGNKEY:
		case PGM_INDEX:
		case PGM_PRIMARYKEY:
		case PGM_TRIGGER:
		case PGM_UNIQUE:
			owneritem=node->GetTable()->GetId();
			break;

		case PGM_RULE: // Rules are technically table objects! Yeuch
		case EDB_PACKAGEFUNCTION:
		case EDB_PACKAGEVARIABLE: 
		case PGM_SCHEDULE:
		case PGM_STEP:
			if (node->IsCollection())
				owneritem = frame->GetBrowser()->GetParentObject(node->GetId())->GetId();
			else
				owneritem = frame->GetBrowser()->GetParentObject(frame->GetBrowser()->GetParentObject(node->GetId())->GetId())->GetId();
			break;

		default:
			break;
	}
}


dlgProperty *dlgProperty::CreateDlg(frmMain *frame, pgObject *node, bool asNew, pgaFactory *factory)
{
    if (!factory)
    {
        factory=node->GetFactory();
        if (node->IsCollection())
            factory = ((pgaCollectionFactory*)factory)->GetItemFactory();
    }

    pgObject *currentNode, *parentNode;
    if (asNew)
        currentNode=0;
    else
        currentNode=node;

    if (factory != node->GetFactory())
        parentNode = node;
    else
        parentNode = frame->GetBrowser()->GetObject(
                     frame->GetBrowser()->GetItemParent(node->GetId()));

    if (parentNode && parentNode->IsCollection() && parentNode->GetMetaType() != PGM_SERVER)
        parentNode = frame->GetBrowser()->GetObject(
                     frame->GetBrowser()->GetItemParent(parentNode->GetId()));

    dlgProperty *dlg=0;

    if (factory)
    {
        dlg = factory->CreateDialog(frame, currentNode, parentNode);
        if (dlg)
        {
            if (factory->IsCollection())
                factory = ((pgaCollectionFactory*)factory)->GetItemFactory();
            wxASSERT(factory);

            dlg->InitDialog(frame, node);
        }
    }
    return dlg;
}


bool dlgProperty::CreateObjectDialog(frmMain *frame, pgObject *node, pgaFactory *factory)
{
    if (node->GetMetaType() != PGM_SERVER)
    {
        pgConn *conn=node->GetConnection();
        if (!conn || conn->GetStatus() != PGCONN_OK || !conn->IsAlive())
            return false;
    }
    dlgProperty *dlg=CreateDlg(frame, node, true, factory);

    if (dlg)
    {
        dlg->SetTitle(wxGetTranslation(dlg->factory->GetNewString()));

        dlg->CreateAdditionalPages();
        dlg->Go();
        dlg->CheckChange();
    }
    else
        wxMessageBox(_("Not implemented."));
    
    return true;
}


bool dlgProperty::EditObjectDialog(frmMain *frame, ctlSQLBox *sqlbox, pgObject *node)
{
    if (node->GetMetaType() != PGM_SERVER)
    {
        pgConn *conn=node->GetConnection();
        if (!conn || conn->GetStatus() != PGCONN_OK || !conn->IsAlive())
            return false;
    }

	// If this is a function or view, hint that the user might want to edit the object in 
	// the query tool.
    if (node->GetMetaType() == PGM_FUNCTION || node->GetMetaType() == PGM_VIEW)
	{
	if (frmHint::ShowHint(frame, HINT_OBJECT_EDITING) == wxID_CANCEL)
		return false;
	}

    dlgProperty *dlg=CreateDlg(frame, node, false);

    if (dlg)
    {
        wxString typeName = dlg->factory->GetTypeName();
        dlg->SetTitle(wxString(wxGetTranslation(typeName)) + wxT(" ") + node->GetFullIdentifier());

        dlg->CreateAdditionalPages();
        dlg->Go();

        dlg->CheckChange();
    }
    else
        wxMessageBox(_("Not implemented."));

    return true;
}

wxString dlgProperty::qtDbString(const wxString &str) 
{ 
	// Use the server aware version if possible
	if (connection)
	    return connection->qtDbString(str);
	else if (database)
		return database->GetConnection()->qtDbString(str);
	else
	{
		wxString ret = str;
		ret.Replace(wxT("\\"), wxT("\\\\"));
        ret.Replace(wxT("'"), wxT("''"));
        ret.Append(wxT("'"));
        ret.Prepend(wxT("'"));
		return ret;
	}
}

void dlgProperty::OnHelp(wxCommandEvent& ev)
{
    wxString page=GetHelpPage();

    if (!page.IsEmpty())
    {
        if (page.StartsWith(wxT("pg/")))
        {
            if (connection)
            {
                if (connection->GetIsEdb())
                    DisplayHelp(page.Mid(3), HELP_ENTERPRISEDB);
                else
                    DisplayHelp(page.Mid(3), HELP_POSTGRESQL);
            }
            else
                DisplayHelp(page.Mid(3), HELP_POSTGRESQL);
        }
        else if (page.StartsWith(wxT("slony/")))
            DisplayHelp(page.Mid(6), HELP_SLONY);
        else
            DisplayHelp(page, HELP_PGADMIN);
    }
}

/////////////////////////////////////////////////////////////////////////////


dlgTypeProperty::dlgTypeProperty(pgaFactory *f, frmMain *frame, const wxString &resName)
: dlgProperty(f, frame, resName)
{
    isVarLen=false;
    isVarPrec=false;
    if (wxWindow::FindWindow(XRCID("txtLength")))
    {
        txtLength = CTRL_TEXT("txtLength");
        txtLength->SetValidator(numericValidator);
        txtLength->Disable();
    }
    else
        txtLength = 0;
    if (wxWindow::FindWindow(XRCID("txtPrecision")))
    {
        txtPrecision= CTRL_TEXT("txtPrecision");
        txtPrecision->SetValidator(numericValidator);
        txtPrecision->Disable();
    }
    else
        txtPrecision=0;
}


void dlgTypeProperty::FillDatatype(ctlComboBox *cb, bool withDomains)
{
    FillDatatype(cb, 0, withDomains);
}


void dlgTypeProperty::FillDatatype(ctlComboBox *cb, ctlComboBox *cb2, bool withDomains)
{
    DatatypeReader tr(database, withDomains);
    while (tr.HasMore())
    {
        pgDatatype dt=tr.GetDatatype();

        AddType(wxT("?"), tr.GetOid(), dt.FullName());
        cb->Append(dt.FullName());
        if (cb2)
            cb2->Append(dt.FullName());
        tr.MoveNext();
    }
}


int dlgTypeProperty::Go(bool modal)
{
    if (GetObject())
    {
        if (txtLength)
            txtLength->SetValidator(numericValidator);
        if (txtPrecision)
            txtPrecision->SetValidator(numericValidator);
    }
    return dlgProperty::Go(modal);
}



void dlgTypeProperty::AddType(const wxString &typ, const OID oid, const wxString quotedName)
{
    wxString vartyp;
    if (typ == wxT("?"))
    {
        switch ((long)oid)
        {
            case PGOID_TYPE_BIT:
            case PGOID_TYPE_BIT_ARRAY:
            case PGOID_TYPE_BPCHAR:
            case PGOID_TYPE_BPCHAR_ARRAY:
            case PGOID_TYPE_VARCHAR:
            case PGOID_TYPE_VARCHAR_ARRAY:
                vartyp=wxT("L");
                break;
            case PGOID_TYPE_TIME:
            case PGOID_TYPE_TIME_ARRAY:
            case PGOID_TYPE_TIMETZ:
            case PGOID_TYPE_TIMETZ_ARRAY:
            case PGOID_TYPE_TIMESTAMP:
            case PGOID_TYPE_TIMESTAMP_ARRAY:
            case PGOID_TYPE_TIMESTAMPTZ:
            case PGOID_TYPE_TIMESTAMPTZ_ARRAY:
            case PGOID_TYPE_INTERVAL:
            case PGOID_TYPE_INTERVAL_ARRAY:
                vartyp=wxT("D");
                break;
            case PGOID_TYPE_NUMERIC:
            case PGOID_TYPE_NUMERIC_ARRAY:
                vartyp=wxT("P");
                break;
            default:
                vartyp=wxT(" ");
                break;
        }
    }
    else
        vartyp=typ;

    types.Add(vartyp + NumToStr(oid) + wxT(":") + quotedName);
}

    
wxString dlgTypeProperty::GetTypeInfo(int sel)
{
    wxString str;
    if (sel >= 0)
        str = types.Item(sel);

    return str;
}


wxString dlgTypeProperty::GetTypeOid(int sel)
{
    wxString str;
    if (sel >= 0)
        str = types.Item(sel).Mid(1).BeforeFirst(':');

    return str;
}


wxString dlgTypeProperty::GetQuotedTypename(int sel)
{
    wxString sql;
    bool isArray = false;

    if (sel >= 0)
    {
        sql = types.Item(sel).AfterFirst(':');
		if (sql.Right(2) == wxT("[]"))
		{
			sql = sql.SubString(0,sql.Len()-3);
			isArray = true;
		}
		else if (sql.Right(3) == wxT("[]\""))
		{
			sql = sql.SubString(1,sql.Len()-4);
			isArray = true;
		}

        if (isVarLen && txtLength)
        {
            wxString varlen=txtLength->GetValue();
            if (!varlen.IsEmpty() && NumToStr(StrToLong(varlen))==varlen && StrToLong(varlen) >= minVarLen)
            {
                sql += wxT("(") + varlen;
                if (isVarPrec && txtPrecision)
                {
                    wxString varprec=txtPrecision->GetValue();
                    if (!varprec.IsEmpty())
                        sql += wxT(", ") + varprec;
                }
                sql += wxT(")");
            }
        }
    }

    if (isArray) sql += wxT("[]");
    return sql;
}


void dlgTypeProperty::CheckLenEnable()
{
    int sel=cbDatatype->GetGuessedSelection();
    if (sel >= 0)
    {
        wxString info=types.Item(sel);
        isVarPrec = info.StartsWith(wxT("P"));
        isVarLen =  isVarPrec || info.StartsWith(wxT("L")) || info.StartsWith(wxT("D"));
        minVarLen = (info.StartsWith(wxT("D")) ? 0 : 1);
        maxVarLen = isVarPrec ? 1000 : 
                    minVarLen ? 0x7fffffff : 10;
    }
}


/////////////////////////////////////////////////////////////////////////////


dlgCollistProperty::dlgCollistProperty(pgaFactory *f, frmMain *frame, const wxString &resName, pgTable *parentNode)
: dlgProperty(f, frame, resName)
{
    columns=0;
    table=parentNode;
}


dlgCollistProperty::dlgCollistProperty(pgaFactory *f, frmMain *frame, const wxString &resName, ctlListView *colList)
: dlgProperty(f, frame, resName)
{
    columns=colList;
    table=0;
}


int dlgCollistProperty::Go(bool modal)
{
    if (columns)
    {
        int pos;
        // iterate cols
        for (pos=0 ; pos < columns->GetItemCount() ; pos++)
        {
            wxString col=columns->GetItemText(pos);
            if (cbColumns->FindString(col) < 0)
                cbColumns->Append(col);
        }
    }
    if (table)
    {
        wxCookieType cookie;
        pgObject *data;
        wxTreeItemId columnsItem=mainForm->GetBrowser()->GetFirstChild(table->GetId(), cookie);
        while (columnsItem)
        {
            data=mainForm->GetBrowser()->GetObject(columnsItem);
            if (data->GetMetaType() == PGM_COLUMN && data->IsCollection())
                break;
            columnsItem=mainForm->GetBrowser()->GetNextChild(table->GetId(), cookie);
        }

        if (columnsItem)
        {
            wxCookieType cookie;
            pgColumn *column;
            wxTreeItemId item=mainForm->GetBrowser()->GetFirstChild(columnsItem, cookie);

            // check columns
            while (item)
            {
                column=(pgColumn*)mainForm->GetBrowser()->GetObject(item);
                if (column->IsCreatedBy(columnFactory))
                {
                    if (column->GetColNumber() > 0)
                    {
                        cbColumns->Append(column->GetName());
                    }
                }
        
                item=mainForm->GetBrowser()->GetNextChild(columnsItem, cookie);
            }
        }
    }
    return dlgProperty::Go(modal);
}



/////////////////////////////////////////////////////////////////////////////


BEGIN_EVENT_TABLE(dlgSecurityProperty, dlgProperty)
    EVT_BUTTON(CTL_ADDPRIV,             dlgSecurityProperty::OnAddPriv)
    EVT_BUTTON(CTL_DELPRIV,             dlgSecurityProperty::OnDelPriv)
END_EVENT_TABLE();


dlgSecurityProperty::dlgSecurityProperty(pgaFactory *f, frmMain *frame, pgObject *obj, const wxString &resName, const wxString& privList, char *privChar)
        : dlgProperty(f, frame, resName)
{
    securityChanged=false;


    if (!privList.IsEmpty() && (!obj || obj->CanCreate()))
    {
        securityPage = new ctlSecurityPanel(nbNotebook, privList, privChar, frame->GetImageList());

        if (obj)
        {
            wxString str=obj->GetAcl();
            if (!str.IsEmpty())
            {
                str = str.Mid(1, str.Length()-2);
                wxStringTokenizer tokens(str, wxT(","));

                while (tokens.HasMoreTokens())
                {
                    wxString str=tokens.GetNextToken();
                    if (str[0U]== '"')
                        str = str.Mid(1, str.Length()-2);

                    wxString name=str.BeforeLast('=');
                    wxString value;

                    connection = obj->GetConnection();
                    if (connection->BackendMinimumVersion(7, 4))
                        value=str.Mid(name.Length()+1).BeforeLast('/');
                    else
                        value=str.Mid(name.Length()+1);

                    int icon=userFactory.GetIconId();

                    if (name.Left(6).IsSameAs(wxT("group "), false))
                    {
                        icon = groupFactory.GetIconId();
                        name = wxT("group ") + qtStrip(name.Mid(6));
                    }
                    else if (name.IsEmpty())
                    {
                        icon = PGICON_PUBLIC;
                        name=wxT("public");
                    }
                    else
                        name = qtStrip(name);

                    securityPage->lbPrivileges->AppendItem(icon, name, value);
                    currentAcl.Add(name + wxT("=") + value);
                }
            }
        }
    }else
    securityPage = NULL;
}


dlgSecurityProperty::~dlgSecurityProperty()
{
}



int dlgSecurityProperty::Go(bool modal)
{
    if (securityPage)
        securityPage->SetConnection(connection);
    
    return dlgProperty::Go(modal);
}


void dlgSecurityProperty::AddGroups(ctlComboBox *comboBox)
{
    if (!((securityPage && securityPage->cbGroups) || comboBox))
        return;

    pgSet *set=connection->ExecuteSet(wxT("SELECT groname FROM pg_group ORDER BY groname"));

    if (set)
    {
        while (!set->Eof())
        {
            if (securityPage && securityPage->cbGroups)
                securityPage->cbGroups->Append(wxT("group ") + set->GetVal(0));
            if (comboBox)
                comboBox->Append(set->GetVal(0));
            set->MoveNext();
        }
        delete set;
    }
}


void dlgSecurityProperty::AddUsers(ctlComboBox *combobox)
{
    if (securityPage && securityPage->cbGroups && settings->GetShowUsersForPrivileges())
    {
        securityPage->stGroup->SetLabel(_("Group/User"));
        dlgProperty::AddUsers(securityPage->cbGroups, combobox);
    }
    else
        dlgProperty::AddUsers(combobox);
}


void dlgSecurityProperty::OnAddPriv(wxCommandEvent &ev)
{
    securityChanged=true;
    EnableOK(btnOK->IsEnabled());
}


void dlgSecurityProperty::OnDelPriv(wxCommandEvent &ev)
{
    securityChanged=true;
    EnableOK(btnOK->IsEnabled());
}


wxString dlgSecurityProperty::GetHelpPage() const
{
    if (nbNotebook->GetSelection() == (int)nbNotebook->GetPageCount()-2)
        return wxT("pg/sql-grant");
    else
        return dlgProperty::GetHelpPage();
}


void dlgSecurityProperty::EnableOK(bool enable)
{
	// Don't enable the OK button if the object isn't yet created,
	// leave that to the object dialog.
    if (securityChanged && GetObject())
    {
        wxString sql=GetSql();
        if (sql.IsEmpty())
        {
            enable=false;
            securityChanged=false;
        }
        else
            enable=true;
    }
    dlgProperty::EnableOK(enable);
}




wxString dlgSecurityProperty::GetGrant(const wxString &allPattern, const wxString &grantObject)
{
    if (securityPage)
        return securityPage->GetGrant(allPattern, grantObject, &currentAcl);
    else
    return wxString();
}

bool dlgSecurityProperty::DisablePrivilege(const wxString &priv) 
{ 
    if (securityPage) 
        return securityPage->DisablePrivilege(priv); 
    else 
        return true; 
}


/////////////////////////////////////////////////////////////////////////////


BEGIN_EVENT_TABLE(dlgAgentProperty, dlgProperty)
    EVT_BUTTON (wxID_OK,                            dlgAgentProperty::OnOK)
END_EVENT_TABLE();

dlgAgentProperty::dlgAgentProperty(pgaFactory *f, frmMain *frame, const wxString &resName)
: dlgProperty(f, frame, resName)
{
    recId=0;
}


wxString dlgAgentProperty::GetSql()
{
    wxString str=GetInsertSql();
    if (!str.IsEmpty())
        str += wxT("\n\n");
    return str + GetUpdateSql();
}



bool dlgAgentProperty::executeSql()
{
    wxString sql;
    bool dataChanged=false;

    sql=GetInsertSql();
    if (!sql.IsEmpty())
    {
        int pos;
        long jobId=0, schId=0, stpId=0;
        if (sql.Contains(wxT("<JobId>")))
        {
            recId = jobId=StrToLong(connection->ExecuteScalar(wxT("SELECT nextval('pgagent.pga_job_jobid_seq');")));
            while ((pos=sql.Find(wxT("<JobId>"))) >= 0)
                sql = sql.Left(pos) + NumToStr(jobId) + sql.Mid(pos+7);
        }
        
        if (sql.Contains(wxT("<SchId>")))
        {
            // Each schedule ID should be unique. This'll need work if a schedule hits more than
            // one table or anything.
            recId = schId=StrToLong(connection->ExecuteScalar(wxT("SELECT nextval('pgagent.pga_schedule_jscid_seq');")));
            while ((pos=sql.Find(wxT("<SchId>"))) >= 0)
            {
                sql = sql.Left(pos) + NumToStr(schId) + sql.Mid(pos+7);
                recId = schId=StrToLong(connection->ExecuteScalar(wxT("SELECT nextval('pgagent.pga_schedule_jscid_seq');")));
            }
        }

        if (sql.Contains(wxT("<StpId>")))
        {
            // Each step ID should be unique. This'll need work if a step hits more than
            // one table or anything.
            recId = stpId=StrToLong(connection->ExecuteScalar(wxT("SELECT nextval('pgagent.pga_jobstep_jstid_seq');")));
            while ((pos=sql.Find(wxT("<StpId>"))) >= 0)
            {
                sql = sql.Left(pos) + NumToStr(stpId) + sql.Mid(pos+7);
                recId = stpId=StrToLong(connection->ExecuteScalar(wxT("SELECT nextval('pgagent.pga_jobstep_jstid_seq');")));
            }
        }

        pgSet *set=connection->ExecuteSet(sql);
        if (set)
        {
            delete set;
        }
        if (!set)
        {
            return false;
        }
        dataChanged=true;
    }

    sql=GetUpdateSql();
    if (!sql.IsEmpty())
    {
        int pos;
        while ((pos=sql.Find(wxT("<JobId>"))) >= 0)
            sql = sql.Left(pos) + NumToStr(recId) + sql.Mid(pos+7);

        long newId;
        if (sql.Contains(wxT("<SchId>")))
        {
            // Each schedule ID should be unique. This'll need work if a schedule hits more than
            // one table or anything.
            newId=StrToLong(connection->ExecuteScalar(wxT("SELECT nextval('pgagent.pga_schedule_jscid_seq');")));
            while ((pos=sql.Find(wxT("<SchId>"))) >= 0)
            {
                sql = sql.Left(pos) + NumToStr(newId) + sql.Mid(pos+7);
                newId=StrToLong(connection->ExecuteScalar(wxT("SELECT nextval('pgagent.pga_schedule_jscid_seq');")));
            }
        }

        if (sql.Contains(wxT("<StpId>")))
        {
            // Each step ID should be unique. This'll need work if a step hits more than
            // one table or anything.
            newId=StrToLong(connection->ExecuteScalar(wxT("SELECT nextval('pgagent.pga_jobstep_jstid_seq');")));
            while ((pos=sql.Find(wxT("<StpId>"))) >= 0)
            {
                sql = sql.Left(pos) + NumToStr(newId) + sql.Mid(pos+7);
                newId=StrToLong(connection->ExecuteScalar(wxT("SELECT nextval('pgagent.pga_jobstep_jstid_seq');")));
            }
        }

        if (!connection->ExecuteVoid(sql))
        {
            // error message is displayed inside ExecuteVoid
            return false;
        }
        dataChanged=true;
    }

    return dataChanged;
}


void dlgAgentProperty::OnOK(wxCommandEvent &ev)
{
	if (!IsUpToDate())
	{
		if (wxMessageBox(wxT("The object has been changed by another user. Do you wish to continue to to try to update it?"), wxT("Overwrite changes?"), wxYES_NO) == wxNO)
			return;
	}

    if (IsModal())
    {
        EndModal(0);
        return;
    }

    connection->ExecuteVoid(wxT("BEGIN TRANSACTION"));

    if (executeSql())
    {
        connection->ExecuteVoid(wxT("COMMIT TRANSACTION"));
        ShowObject();
    }
    else
    {
        connection->ExecuteVoid(wxT("ROLLBACK TRANSACTION"));
    }

    Destroy();
}


propertyFactory::propertyFactory(menuFactoryList *list, wxMenu *mnu, wxToolBar *toolbar) : contextActionFactory(list)
{
    if (mnu)
        mnu->Append(id, _("&Properties..."), _("Display/edit the properties of the selected object."));
    else
        context=false;
    if (toolbar)
        toolbar->AddTool(id, _("Properties"), wxBitmap(properties_xpm), _("Display/edit the properties of the selected object."), wxITEM_NORMAL);
}


wxWindow *propertyFactory::StartDialog(frmMain *form, pgObject *obj)
{
    if (!dlgProperty::EditObjectDialog(form, form->GetSqlPane(), obj))
        form->CheckAlive();

    return 0;
}


bool propertyFactory::CheckEnable(pgObject *obj)
{
    return obj && ((obj->GetMetaType() == PGM_DATABASE) ? (obj->GetConnection() != NULL) : true) && obj->CanEdit();
}


#include "images/create.xpm"
createFactory::createFactory(menuFactoryList *list, wxMenu *mnu, wxToolBar *toolbar) : actionFactory(list)
{
    mnu->Append(id, _("&Create..."),  _("Create a new object of the same type as the selected object."));
    toolbar->AddTool(id, _("Create"), wxBitmap(create_xpm), _("Create a new object of the same type as the selected object."), wxITEM_NORMAL);
}


wxWindow *createFactory::StartDialog(frmMain *form, pgObject *obj)
{
    if (!dlgProperty::CreateObjectDialog(form, obj, 0))
        form->CheckAlive();

    return 0;
}


bool createFactory::CheckEnable(pgObject *obj)
{
    return obj && obj->CanCreate();
}


#include "images/drop.xpm"
dropFactory::dropFactory(menuFactoryList *list, wxMenu *mnu, wxToolBar *toolbar) : contextActionFactory(list)
{
    mnu->Append(id, _("&Delete/Drop\tDel"),  _("Delete/Drop the selected object."));
    toolbar->AddTool(id, _("Drop"), wxBitmap(drop_xpm), _("Drop the currently selected object."), wxITEM_NORMAL);
}


wxWindow *dropFactory::StartDialog(frmMain *form, pgObject *obj)
{
    form->ExecDrop(false);
    return 0;
}


bool dropFactory::CheckEnable(pgObject *obj)
{
    return obj && obj->CanDrop();
}


dropCascadedFactory::dropCascadedFactory(menuFactoryList *list, wxMenu *mnu, wxToolBar *toolbar) : contextActionFactory(list)
{
    mnu->Append(id, _("Drop Cascaded"), _("Drop the selected object and all objects dependent on it."));
}


wxWindow *dropCascadedFactory::StartDialog(frmMain *form, pgObject *obj)
{
    form->ExecDrop(true);
    return 0;
}


bool dropCascadedFactory::CheckEnable(pgObject *obj)
{
    return obj && obj->CanDrop() && obj->CanDropCascaded();
}


#include "images/refresh.xpm"
refreshFactory::refreshFactory(menuFactoryList *list, wxMenu *mnu, wxToolBar *toolbar) : contextActionFactory(list)
{
    if (mnu)
        mnu->Append(id, _("Re&fresh\tF5"), _("Refresh the selected object."));
    else
        context = false;
    if (toolbar)
        toolbar->AddTool(id, _("Refresh"), wxBitmap(refresh_xpm), _("Refresh the selected object."), wxITEM_NORMAL);
}


wxWindow *refreshFactory::StartDialog(frmMain *form, pgObject *obj)
{
    if (form) 
       obj = form->GetBrowser()->GetObject(form->GetBrowser()->GetSelection()); 

    if (obj) 
        form->Refresh(obj);
    return 0;
}


bool refreshFactory::CheckEnable(pgObject *obj)
{
    // This isn't really clean... But we don't have a pgObject::CanRefresh() so far,
    // so it's Good Enough (tm) for now.
    return obj != 0 && !obj->IsCreatedBy(serverFactory.GetCollectionFactory());
}
