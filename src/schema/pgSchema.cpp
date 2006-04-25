//////////////////////////////////////////////////////////////////////////
//
// pgAdmin III - PostgreSQL Tools
// RCS-ID:      $Id: pgSchema.cpp 4936 2006-01-19 14:13:54Z dpage $
// Copyright (C) 2002 - 2006, The pgAdmin Development Team
// This software is released under the Artistic Licence
//
// pgSchema.cpp - schema class
//
//////////////////////////////////////////////////////////////////////////

// wxWindows headers
#include <wx/wx.h>

// App headers
#include "pgAdmin3.h"
#include "menu.h"
#include "misc.h"
#include "pgSchema.h"
#include "frmMain.h"
#include "pgDomain.h"
#include "pgAggregate.h"
#include "pgConversion.h"
#include "pgFunction.h"
#include "pgType.h"
#include "pgSequence.h"
#include "pgTable.h"
#include "pgOperator.h"
#include "pgOperatorClass.h"
#include "pgView.h"
#include "frmReport.h"

#include "wx/regex.h"

pgSchema::pgSchema(const wxString& newName)
: pgDatabaseObject(schemaFactory, newName)
{
    wxLogInfo(wxT("Creating a pgSchema object"));
}

pgSchema::~pgSchema()
{
    wxLogInfo(wxT("Destroying a pgSchema object"));
}


wxMenu *pgSchema::GetNewMenu()
{
    wxMenu *menu=pgObject::GetNewMenu();

    if (GetCreatePrivilege())
    {
        aggregateFactory.AppendMenu(menu);
        conversionFactory.AppendMenu(menu);
        domainFactory.AppendMenu(menu);
        functionFactory.AppendMenu(menu);
        triggerFunctionFactory.AppendMenu(menu);
        if (GetConnection()->BackendMinimumVersion(8, 1) || GetConnection()->EdbMinimumVersion(8, 0))
            procedureFactory.AppendMenu(menu);
        operatorFactory.AppendMenu(menu);
//      opclass
        sequenceFactory.AppendMenu(menu);
        tableFactory.AppendMenu(menu);
        typeFactory.AppendMenu(menu);
        viewFactory.AppendMenu(menu);
    }
    return menu;
}

wxMenu *pgSchema::GetReportMenu()
{
    wxMenu *menu=pgObject::GetReportMenu();

    menu->Append(MNU_REPORTS_PROPERTIES, wxT("Properties report..."));

    return menu;
}

void pgSchema::CreateReport(wxWindow *parent, int type)
{
    wxString title, header;

    wxDateTime now = wxDateTime::Now();

    frmReport *rep = new frmReport(parent);

    switch (type)
    {
        case MNU_REPORTS_PROPERTIES:

            rep->AddReportHeaderValue(_("Report generated at"), now.Format(wxT("%c")));
            rep->AddReportHeaderValue(_("Server"), this->GetServer()->GetFullIdentifier());
            rep->AddReportHeaderValue(_("Server"), this->GetDatabase()->GetFullIdentifier());

            title = _("Schema properties report - ");
            title += GetIdentifier();
            rep->SetReportTitle(title);

            rep->AddReportDetailHeader4(_("Schema properties"));

            rep->StartReportTable();
            rep->AddReportPropertyTableRow(_("Name"), this->GetFullIdentifier());
            rep->AddReportPropertyTableRow(_("OID"), NumToStr(this->GetOid()));
            rep->AddReportPropertyTableRow(_("Owner"), this->GetOwner());
            rep->AddReportPropertyTableRow(_("ACL"), this->GetAcl());
            rep->AddReportPropertyTableRow(_("System schema?"), this->GetSystemObject() ? wxT("Yes") : wxT("No"));
            rep->AddReportPropertyTableRow(_("Comment"), this->GetComment());
            rep->EndReportTable();

            rep->AddReportSql(this->GetSql(NULL));

            break;

        default:
            wxLogError(__("The selected report cannot be found for this object type!"));
            delete rep;
            return;
            break;
    }

    rep->ShowModal();
}

bool pgSchema::DropObject(wxFrame *frame, ctlTree *browser, bool cascaded)
{
    wxString sql = wxT("DROP SCHEMA ") + GetQuotedFullIdentifier();
    if (cascaded)
        sql += wxT(" CASCADE");
    return GetDatabase()->ExecuteVoid(sql);
}


wxString pgSchema::GetSql(ctlTree *browser)
{
    if (sql.IsNull())
    {
        sql = wxT("-- Schema: \"") + GetName() + wxT("\"\n\n")
            + wxT("-- DROP SCHEMA ") + GetQuotedFullIdentifier() + wxT(";")
            + wxT("\n\nCREATE SCHEMA ") + qtIdent(GetName()) 
            + wxT("\n  AUTHORIZATION ") + qtIdent(GetOwner());

        sql += wxT(";\n")
            + GetGrant(wxT("UC"), wxT("SCHEMA ") + GetQuotedFullIdentifier())
            + GetCommentSql();
    }
    return sql;
}


void pgSchema::ShowTreeDetail(ctlTree *browser, frmMain *form, ctlListView *properties, ctlSQLBox *sqlPane)
{
//    if (form)
//        form->SetDatabase(GetDatabase());

    GetDatabase()->GetServer()->iSetLastDatabase(GetDatabase()->GetName());
    GetDatabase()->GetServer()->iSetLastSchema(GetName());

    if (!expandedKids)
    {
        expandedKids=true;

        browser->RemoveDummyChild(this);

        // Log
        wxLogInfo(wxT("Adding child object to schema ") + GetIdentifier());

        
        browser->AppendCollection(this, aggregateFactory);
        browser->AppendCollection(this, conversionFactory);
        browser->AppendCollection(this, domainFactory);
        browser->AppendCollection(this, functionFactory);
        browser->AppendCollection(this, triggerFunctionFactory);

        if (GetConnection()->BackendMinimumVersion(8, 1) || GetConnection()->EdbMinimumVersion(8, 0))
            browser->AppendCollection(this, procedureFactory);

        browser->AppendCollection(this, operatorFactory);
        browser->AppendCollection(this, operatorClassFactory);
        browser->AppendCollection(this, sequenceFactory);
        browser->AppendCollection(this, tableFactory);
        browser->AppendCollection(this, typeFactory);
        browser->AppendCollection(this, viewFactory);
    }


    if (properties)
    {
        wxLogInfo(wxT("Displaying properties for schema ") + GetIdentifier());

        CreateListColumns(properties);

        properties->AppendItem(_("Name"), GetName());
        properties->AppendItem(_("OID"), GetOid());
        properties->AppendItem(_("Owner"), GetOwner());
        properties->AppendItem(_("ACL"), GetAcl());
        properties->AppendItem(_("System schema?"), GetSystemObject());
        properties->AppendItem(_("Comment"), GetComment());
    }
}



pgObject *pgSchema::Refresh(ctlTree *browser, const wxTreeItemId item)
{
    pgObject *schema=0;
    pgCollection *coll=browser->GetParentCollection(item);
    if (coll)
        schema = schemaFactory.CreateObjects(coll, 0, wxT(" WHERE nsp.oid=") + GetOidStr() + wxT("\n"));

    return schema;
}



pgObject *pgSchemaFactory::CreateObjects(pgCollection *collection, ctlTree *browser, const wxString &restriction)
{
    pgSchema *schema=0;

    wxString restr=restriction;

    if (!collection->GetDatabase()->GetSchemaRestriction().IsEmpty())
    {
        if (restr.IsEmpty())
            restr += wxT(" WHERE (");
        else
            restr += wxT("   AND (");
        restr += collection->GetDatabase()->GetSchemaRestriction() + wxT(")");
    }

    pgSet *schemas = collection->GetDatabase()->ExecuteSet(
        wxT("SELECT CASE WHEN nspname LIKE 'pg\\_temp\\_%%' THEN 1\n")
        wxT("            WHEN (nspname LIKE 'pg\\_%' OR nspname = 'information_schema') THEN 0\n")
        wxT("            ELSE 3 END AS nsptyp,\n")
        wxT("       nsp.nspname, nsp.oid, pg_get_userbyid(nspowner) AS namespaceowner, nspacl, description,")
        wxT("       has_schema_privilege(nsp.oid, 'CREATE') as cancreate\n")
        wxT("  FROM pg_namespace nsp\n")
        wxT("  LEFT OUTER JOIN pg_description des ON des.objoid=nsp.oid\n")
         + restr +
        wxT(" ORDER BY 1, nspname"));

    if (schemas)
    {
        while (!schemas->Eof())
        {
            wxString name=schemas->GetVal(wxT("nspname"));
            long nsptyp=schemas->GetLong(wxT("nsptyp"));

            wxStringTokenizer tokens(settings->GetSystemSchemas(), wxT(","));
            while (tokens.HasMoreTokens())
            {
                wxRegEx regex(tokens.GetNextToken());
                if (regex.Matches(name))
                {
                    nsptyp = SCHEMATYP_USERSYS;
                    break;
                }
            }

            if (nsptyp <= SCHEMATYP_USERSYS && !settings->GetShowSystemObjects())
            {
                schemas->MoveNext();
                continue;
            }

            schema = new pgSchema(name);
            schema->iSetSchemaTyp(nsptyp);
            schema->iSetDatabase(collection->GetDatabase());
            schema->iSetComment(schemas->GetVal(wxT("description")));
            schema->iSetOid(schemas->GetOid(wxT("oid")));
            schema->iSetOwner(schemas->GetVal(wxT("namespaceowner")));
            schema->iSetAcl(schemas->GetVal(wxT("nspacl")));
            schema->iSetCreatePrivilege(schemas->GetBool(wxT("cancreate")));

            if (browser)
            {
                browser->AppendObject(collection, schema);
				schemas->MoveNext();
            }
            else
                break;
        }

		delete schemas;
    }
    return schema;
}


pgObject *pgSchema::ReadObjects(pgCollection *collection, ctlTree *browser)
{
    wxString systemRestriction;
    if (!settings->GetShowSystemObjects())
        systemRestriction = wxT("WHERE ") + collection->GetConnection()->SystemNamespaceRestriction(wxT("nsp.nspname"));

    // Get the schemas
    return schemaFactory.CreateObjects(collection, browser, systemRestriction);
}


/////////////////////////////////////////////////////

pgSchemaObjCollection::pgSchemaObjCollection(pgaFactory *factory, pgSchema *sch)
: pgCollection(factory)
{ 
    schema = sch;
    database = schema->GetDatabase();
    server= database->GetServer();
    iSetOid(sch->GetOid());
}


bool pgSchemaObjCollection::CanCreate()
{
    return GetSchema()->GetCreatePrivilege();
}


#include "images/namespace.xpm"
#include "images/namespace-sm.xpm"
#include "images/namespaces.xpm"

pgSchemaFactory::pgSchemaFactory() 
: pgDatabaseObjFactory(__("Schema"), __("New Schema..."), __("Create a new Schema."), namespace_xpm, namespace_sm_xpm)
{
    metaType = PGM_SCHEMA;
}

pgCollection *pgSchemaObjFactory::CreateCollection(pgObject *obj)
{
    return new pgSchemaObjCollection(GetCollectionFactory(), (pgSchema*)obj);
}


pgSchemaFactory schemaFactory;
static pgaCollectionFactory cf(&schemaFactory, __("Schemas"), namespaces_xpm);
