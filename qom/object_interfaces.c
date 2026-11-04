#include "qemu/osdep.h"

#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qobject/qobject.h"
#include "qobject/qbool.h"
#include "qobject/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qobject/qjson.h"
#include "qobject/qstring.h"
#include "qapi/qobject-input-visitor.h"
#include "qom/object_interfaces.h"
#include "qemu/help_option.h"
#include "qemu/id.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/qemu-print.h"
#include "qapi/opts-visitor.h"
#include "qemu/config-file.h"
#include "qemu/keyval.h"

bool user_creatable_complete(UserCreatable *uc, Error **errp)
{
    UserCreatableClass *ucc = USER_CREATABLE_GET_CLASS(uc);
    ERRP_GUARD();

    if (ucc->complete) {
        ucc->complete(uc, errp);
    }
    return !*errp;
}

static void object_set_properties_from_qdict(Object *obj, const QDict *qdict,
                                             Visitor *v, Error **errp)
{
    const QDictEntry *e;

    if (!visit_start_struct(v, NULL, NULL, 0, errp)) {
        return;
    }
    for (e = qdict_first(qdict); e; e = qdict_next(qdict, e)) {
        if (!object_property_set(obj, e->key, v, errp)) {
            goto out;
        }
    }
    visit_check_struct(v, errp);
out:
    visit_end_struct(v, NULL);
}

void object_set_properties_from_keyval(Object *obj, const QDict *qdict,
                                       bool from_json, Error **errp)
{
    Visitor *v;
    if (from_json) {
        v = qobject_input_visitor_new(QOBJECT(qdict));
    } else {
        v = qobject_input_visitor_new_keyval(QOBJECT(qdict));
    }
    object_set_properties_from_qdict(obj, qdict, v, errp);
    visit_free(v);
}

static void register_types(void)
{
    static const TypeInfo uc_interface_info = {
        .name          = TYPE_USER_CREATABLE,
        .parent        = TYPE_INTERFACE,
        .class_size = sizeof(UserCreatableClass),
    };

    type_register_static(&uc_interface_info);
}

type_init(register_types)
