#ifndef LOCKBOX_ACTION_H
#define LOCKBOX_ACTION_H

struct Action {
    const char* name = "";
    bool hold = false;
    bool visible = true;
};

#endif  // LOCKBOX_ACTION_H
