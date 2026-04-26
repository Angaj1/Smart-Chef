#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Link flags for GCC:  -lcomctl32
   (MSVC users: add comctl32.lib manually in project settings) */

/* ════════════════════════════════════════════════════════════
   COLOUR PALETTE
════════════════════════════════════════════════════════════ */
#define C_BG RGB(14, 17, 26)      /* main window background       */
#define C_PANEL RGB(22, 27, 42)   /* section panel fill            */
#define C_GOLD RGB(248, 176, 62)  /* accent – labels, borders, btn */
#define C_TEXT RGB(225, 228, 240) /* primary text                  */
#define C_LIST RGB(18, 22, 36)    /* listbox / edit background     */
#define C_BORDER RGB(48, 55, 85)  /* panel outline                 */
#define C_BTNBG RGB(30, 36, 58)   /* button normal bg              */
#define C_BTNHOV RGB(42, 52, 82)  /* button hover bg               */
#define C_BTNSEL RGB(55, 68, 108) /* button pressed bg             */

/* ════════════════════════════════════════════════════════════
   DATA STRUCTURES  
════════════════════════════════════════════════════════════ */
typedef struct Ingredient
{
    char name[30];
    struct Ingredient *next;
} Ingredient;

typedef struct Recipe
{
    char title[50];
    Ingredient *ingredients;
    char utensils[100];
    char skillLevel[20];
    int prepTime;
    struct Recipe *next;
} Recipe;

typedef struct FridgeNode
{
    char name[30];
    struct FridgeNode *left, *right;
} FridgeNode;

typedef struct QueueNode
{
    char title[50];
    int prepTime;
    struct QueueNode *next;
} QueueNode;

typedef struct ShoppingNode
{
    char name[30];
    struct ShoppingNode *prev, *next;
} ShoppingNode;

typedef struct FeaturedNode
{
    char title[50];
    struct FeaturedNode *next;
} FeaturedNode;

/* ════════════════════════════════════════════════════════════
   GLOBAL STATE
════════════════════════════════════════════════════════════ */
static FridgeNode *fridgeRoot = NULL;
static Recipe *masterList = NULL;
static QueueNode *qFront = NULL;
static ShoppingNode *shopHead = NULL, *shopTail = NULL;
static FeaturedNode *featHead = NULL, *featCurr = NULL;

#define MAX_HIST 5
static char histStack[MAX_HIST][50];
static int histTop = -1;

/* GDI handles */
static HFONT hFUI, hFBold;
static HBRUSH hBrBg, hBrPanel, hBrList;

/* Control handles */
static HWND hEditIng, hListRecipes, hEditDetails;
static HWND hListFridge, hListQueue, hListHist, hListShop;
static HWND hLblFeat, hStatus;

/* Button / control IDs */
#define ID_ADD_FRIDGE 1
#define ID_LB_RECIPES 2
#define ID_CHECK 3
#define ID_ADD_QUEUE 4
#define ID_ADD_SHOP 5
#define ID_NEXT_FEAT 6
#define ID_REM_QUEUE 7
#define ID_CLEAR_SHOP 8

/* ════════════════════════════════════════════════════════════
   UI HELPERS
════════════════════════════════════════════════════════════ */
static void SetStatus(const char *msg)
{
    if (hStatus)
        SendMessage(hStatus, SB_SETTEXT, 0, (LPARAM)msg);
}

/* Owner-draw button (gold border + hover/press colouring) */
static HWND ODBtnCreate(HWND p, const char *t, int x, int y, int w, int h, int id)
{
    HWND b = CreateWindow("BUTTON", t,
                          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                          x, y, w, h, p, (HMENU)(UINT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessage(b, WM_SETFONT, (WPARAM)hFUI, TRUE);
    return b;
}

/* Dark-themed listbox (no WS_BORDER – panel provides the frame) */
static HWND LBCreate(HWND p, int x, int y, int w, int h, int id)
{
    HWND lb = CreateWindow("LISTBOX", NULL,
                           WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                           x, y, w, h, p, (HMENU)(UINT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessage(lb, WM_SETFONT, (WPARAM)hFUI, TRUE);
    return lb;
}

/* Static label */
static HWND LblCreate(HWND p, const char *t, int x, int y, int w, int h, HFONT f)
{
    HWND lbl = CreateWindow("STATIC", t, WS_VISIBLE | WS_CHILD | SS_LEFT,
                            x, y, w, h, p, NULL, NULL, NULL);
    SendMessage(lbl, WM_SETFONT, (WPARAM)f, TRUE);
    return lbl;
}

/* Draw a filled panel rect with a 1-px border */
static void PanelDraw(HDC dc, int x, int y, int w, int h)
{
    RECT r = {x, y, x + w, y + h};
    HBRUSH fill = CreateSolidBrush(C_PANEL);
    FillRect(dc, &r, fill);
    DeleteObject(fill);

    HPEN pen = CreatePen(PS_SOLID, 1, C_BORDER);
    HPEN opn = (HPEN)SelectObject(dc, pen);
    HBRUSH onb = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, x, y, x + w, y + h);
    SelectObject(dc, opn);
    SelectObject(dc, onb);
    DeleteObject(pen);
}

/* ════════════════════════════════════════════════════════════
   DATA LOGIC
════════════════════════════════════════════════════════════ */
static void AddRecipe(char *title, char *utensils, char *skill,
                      int time, char *ings[], int n)
{
    Recipe *r = (Recipe *)malloc(sizeof(Recipe));
    strcpy(r->title, title);
    strcpy(r->utensils, utensils);
    strcpy(r->skillLevel, skill);
    r->prepTime = time;
    r->ingredients = NULL;
    for (int i = n - 1; i >= 0; i--)
    {
        Ingredient *g = (Ingredient *)malloc(sizeof(Ingredient));
        strcpy(g->name, ings[i]);
        g->next = r->ingredients;
        r->ingredients = g;
    }
    r->next = masterList;
    masterList = r;
}

static void SortRecipes(void)
{
    if (!masterList)
        return;
    int sw;
    Recipe *lp = NULL;
    do
    {
        sw = 0;
        Recipe *p = masterList;
        while (p->next != lp)
        {
            if (stricmp(p->title, p->next->title) > 0)
            {
                char tT[50], tU[100], tS[20];
                int tTime = p->prepTime;
                Ingredient *tI = p->ingredients;
                strcpy(tT, p->title);
                strcpy(tU, p->utensils);
                strcpy(tS, p->skillLevel);
                strcpy(p->title, p->next->title);
                strcpy(p->utensils, p->next->utensils);
                strcpy(p->skillLevel, p->next->skillLevel);
                p->prepTime = p->next->prepTime;
                p->ingredients = p->next->ingredients;
                strcpy(p->next->title, tT);
                strcpy(p->next->utensils, tU);
                strcpy(p->next->skillLevel, tS);
                p->next->prepTime = tTime;
                p->next->ingredients = tI;
                sw = 1;
            }
            p = p->next;
        }
        lp = p;
    } while (sw);
}

static FridgeNode *FridgeInsert(FridgeNode *root, char *name)
{
    if (!root)
    {
        FridgeNode *n = (FridgeNode *)malloc(sizeof(FridgeNode));
        strcpy(n->name, name);
        n->left = n->right = NULL;
        return n;
    }
    int c = stricmp(name, root->name);
    if (c < 0)
        root->left = FridgeInsert(root->left, name);
    else if (c > 0)
        root->right = FridgeInsert(root->right, name);
    return root;
}

static int FridgeSearch(FridgeNode *root, char *name)
{
    if (!root)
        return 0;
    int c = stricmp(name, root->name);
    if (c == 0)
        return 1;
    return c < 0 ? FridgeSearch(root->left, name) : FridgeSearch(root->right, name);
}

static void FridgeSaveDFS(FridgeNode *root, FILE *fp)
{
    if (!root)
        return;
    fprintf(fp, "%s\n", root->name);
    FridgeSaveDFS(root->left, fp);
    FridgeSaveDFS(root->right, fp);
}

static void FridgeLoad(void)
{
    FILE *fp = fopen("fridge.txt", "r");
    if (!fp)
        return;
    char buf[30];
    while (fgets(buf, sizeof(buf), fp))
    {
        buf[strcspn(buf, "\r\n")] = 0; /* strip newline safely */
        if (buf[0])
            fridgeRoot = FridgeInsert(fridgeRoot, buf);
    }
    fclose(fp);
}

static void FridgeDisplay(FridgeNode *root, HWND lb)
{
    if (!root)
        return;
    FridgeDisplay(root->left, lb);
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)root->name);
    FridgeDisplay(root->right, lb);
}

static int Enqueue(char *title, int time)
{
    QueueNode *c = qFront;
    while (c)
    {
        if (stricmp(c->title, title) == 0)
            return 0;
        c = c->next;
    }
    QueueNode *n = (QueueNode *)malloc(sizeof(QueueNode));
    strcpy(n->title, title);
    n->prepTime = time;
    n->next = NULL;
    if (!qFront || time < qFront->prepTime)
    {
        n->next = qFront;
        qFront = n;
    }
    else
    {
        QueueNode *cur = qFront;
        while (cur->next && cur->next->prepTime <= time)
            cur = cur->next;
        n->next = cur->next;
        cur->next = n;
    }
    return 1;
}

static void QueueRefresh(void)
{
    SendMessage(hListQueue, LB_RESETCONTENT, 0, 0);
    QueueNode *q = qFront;
    char buf[80];
    while (q)
    {
        sprintf(buf, "%-26s  %d min", q->title, q->prepTime);
        SendMessage(hListQueue, LB_ADDSTRING, 0, (LPARAM)buf);
        q = q->next;
    }
}

static int AddToShop(char *item)
{
    ShoppingNode *c = shopHead;
    while (c)
    {
        if (stricmp(c->name, item) == 0)
            return 0;
        c = c->next;
    }
    ShoppingNode *n = (ShoppingNode *)malloc(sizeof(ShoppingNode));
    strcpy(n->name, item);
    n->next = NULL;
    n->prev = shopTail;
    if (!shopHead)
        shopHead = shopTail = n;
    else
    {
        shopTail->next = n;
        shopTail = n;
    }
    return 1;
}

static void ShopClear(void)
{
    ShoppingNode *cur = shopHead;
    while (cur)
    {
        ShoppingNode *nx = cur->next;
        free(cur);
        cur = nx;
    }
    shopHead = shopTail = NULL;
    SendMessage(hListShop, LB_RESETCONTENT, 0, 0);
    SetStatus("  Shopping list cleared.");
}

static void PushHistory(char *title)
{
    if (histTop < MAX_HIST - 1)
        strcpy(histStack[++histTop], title);
    else
    {
        for (int i = 0; i < MAX_HIST - 1; i++)
            strcpy(histStack[i], histStack[i + 1]);
        strcpy(histStack[MAX_HIST - 1], title);
    }
}

static void AddFeatured(char *title)
{
    FeaturedNode *n = (FeaturedNode *)malloc(sizeof(FeaturedNode));
    strcpy(n->title, title);
    if (!featHead)
    {
        featHead = n;
        n->next = featHead;
    }
    else
    {
        FeaturedNode *c = featHead;
        while (c->next != featHead)
            c = c->next;
        c->next = n;
        n->next = featHead;
    }
}

/* ════════════════════════════════════════════════════════════
   WINDOW PROCEDURE
════════════════════════════════════════════════════════════ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {

    /* ── CREATION ──────────────────────────────────────────── */
    case WM_CREATE:
    {
        /* GDI font & brush resources */
        hFUI = CreateFont(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                          ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        hFBold = CreateFont(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                            ANSI_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        hBrBg = CreateSolidBrush(C_BG);
        hBrPanel = CreateSolidBrush(C_PANEL);
        hBrList = CreateSolidBrush(C_LIST);

        /* ── Seed recipes (all 14) ── */
        {
            char *i01[] = {"Egg", "Salt", "Pepper"};
            AddRecipe("Omelette", "Frying Pan", "Beginner", 10, i01, 3);
            char *i02[] = {"Apple", "Banana", "Honey"};
            AddRecipe("Fruit Salad", "Knife, Bowl", "Novice", 5, i02, 3);
            char *i03[] = {"Pasta", "Tomato", "Garlic"};
            AddRecipe("Spaghetti", "Pot", "Intermediate", 20, i03, 3);
            char *i04[] = {"Bread", "Cheese", "Butter"};
            AddRecipe("Grilled Cheese", "Frying Pan", "Beginner", 8, i04, 3);
            char *i05[] = {"Flour", "Milk", "Egg", "Syrup"};
            AddRecipe("Pancakes", "Frying Pan, Bowl", "Beginner", 15, i05, 4);
            char *i06[] = {"Lettuce", "Croutons", "Chicken", "Dressing"};
            AddRecipe("Caesar Salad", "Bowl, Knife", "Beginner", 10, i06, 4);
            char *i07[] = {"Beef", "Broccoli", "Soy Sauce", "Garlic"};
            AddRecipe("Beef Stir Fry", "Wok", "Intermediate", 20, i07, 4);
            char *i08[] = {"Chicken", "Onion", "Curry Powder", "Rice"};
            AddRecipe("Chicken Curry", "Pot", "Intermediate", 30, i08, 4);
            char *i09[] = {"Tomato", "Onion", "Cream", "Broth"};
            AddRecipe("Tomato Soup", "Pot, Blender", "Beginner", 25, i09, 4);
            char *i10[] = {"Bread", "Avocado", "Salt", "Lemon"};
            AddRecipe("Avocado Toast", "Toaster", "Beginner", 5, i10, 4);
            char *i11[] = {"Beef", "Taco Shells", "Cheese", "Salsa"};
            AddRecipe("Tacos", "Pan", "Beginner", 15, i11, 4);
            char *i12[] = {"Rice", "Mushroom", "Broth", "Parmesan"};
            AddRecipe("Mushroom Risotto", "Pot", "Advanced", 40, i12, 4);
            char *i13[] = {"Salmon", "Lemon", "Garlic", "Asparagus"};
            AddRecipe("Baked Salmon", "Oven Tray", "Intermediate", 25, i13, 4);
            char *i14[] = {"Banana", "Milk", "Yogurt", "Berries"};
            AddRecipe("Berry Smoothie", "Blender", "Beginner", 5, i14, 4);
        }
        SortRecipes();
        FridgeLoad();

        AddFeatured("Omelette");
        AddFeatured("Pancakes");
        AddFeatured("Spaghetti");
        AddFeatured("Beef Stir Fry");
        AddFeatured("Mushroom Risotto");
        AddFeatured("Avocado Toast");
        featCurr = featHead;

        /* ════ TOP ROW ════
           Panel 1  Fridge   : x=15  y=33  w=218  h=302
           Panel 2  Recipes  : x=241 y=33  w=222  h=302
           Panel 3  Details  : x=471 y=33  w=474  h=302   */

        /* --- FRIDGE PANEL --- */
        LblCreate(hwnd, "YOUR FRIDGE  (BST)", 25, 43, 195, 18, hFBold);
        LblCreate(hwnd, "Add ingredient:", 25, 71, 120, 17, hFUI);
        hEditIng = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                                  WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
                                  25, 91, 136, 24, hwnd, NULL, NULL, NULL);
        SendMessage(hEditIng, WM_SETFONT, (WPARAM)hFUI, TRUE);
        ODBtnCreate(hwnd, "Add", 168, 91, 50, 24, ID_ADD_FRIDGE);
        hListFridge = LBCreate(hwnd, 25, 124, 197, 198, 0);
        FridgeDisplay(fridgeRoot, hListFridge);

        /* --- RECIPES PANEL --- */
        LblCreate(hwnd, "RECIPES  (A-Z Sorted)", 251, 43, 200, 18, hFBold);
        ODBtnCreate(hwnd, "Check What I Can Cook", 251, 71, 200, 26, ID_CHECK);
        hListRecipes = LBCreate(hwnd, 251, 105, 200, 217, ID_LB_RECIPES);

        /* --- DETAILS PANEL --- */
        LblCreate(hwnd, "RECIPE DETAILS", 481, 43, 400, 18, hFBold);
        hEditDetails = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT",
                                      "Select a recipe to view its details.",
                                      WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                      481, 71, 455, 194, hwnd, NULL, NULL, NULL);
        SendMessage(hEditDetails, WM_SETFONT, (WPARAM)hFUI, TRUE);
        ODBtnCreate(hwnd, "Add to Cooking Queue", 481, 274, 215, 28, ID_ADD_QUEUE);
        ODBtnCreate(hwnd, "Add Missing Items to Shop", 706, 274, 235, 28, ID_ADD_SHOP);

        /* ════ BOTTOM ROW ════
           Panel 4  Queue    : x=15  y=333  w=288  h=218
           Panel 5  Shop     : x=311 y=333  w=222  h=218
           Panel 6  History  : x=541 y=333  w=200  h=218
           Panel 7  Featured : x=749 y=333  w=213  h=218   */

        /* --- QUEUE PANEL --- */
        LblCreate(hwnd, "COOKING QUEUE  (Priority)", 25, 343, 275, 18, hFBold);
        hListQueue = LBCreate(hwnd, 25, 368, 275, 140, 0);
        ODBtnCreate(hwnd, "Remove Selected", 25, 517, 135, 24, ID_REM_QUEUE);

        /* --- SHOPPING LIST PANEL --- */
        LblCreate(hwnd, "SHOPPING LIST  (DLL)", 321, 343, 205, 18, hFBold);
        hListShop = LBCreate(hwnd, 321, 368, 205, 140, 0);
        ODBtnCreate(hwnd, "Clear All", 321, 517, 90, 24, ID_CLEAR_SHOP);

        /* --- HISTORY PANEL --- */
        LblCreate(hwnd, "VIEW HISTORY  (Stack)", 551, 343, 185, 18, hFBold);
        hListHist = LBCreate(hwnd, 551, 368, 185, 140, 0);

        /* --- FEATURED PANEL --- */
        LblCreate(hwnd, "FEATURED  (Circular)", 759, 343, 195, 18, hFBold);
        ODBtnCreate(hwnd, "Next >", 759, 368, 75, 24, ID_NEXT_FEAT);
        hLblFeat = CreateWindow("STATIC", "Click Next >",
                                WS_VISIBLE | WS_CHILD | SS_LEFT,
                                759, 403, 195, 105, hwnd, NULL, NULL, NULL);
        SendMessage(hLblFeat, WM_SETFONT, (WPARAM)hFBold, TRUE);

        /* Status bar */
        hStatus = CreateStatusWindow(
            WS_VISIBLE | WS_CHILD | SBARS_SIZEGRIP,
            "  Ready  |  Smart Chef Ultimate Edition",
            hwnd, 9999);
        break;
    }

    /* ── COMMANDS ───────────────────────────────────────────── */
    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        int notif = HIWORD(wp);

        /* Add ingredient to fridge */
        if (id == ID_ADD_FRIDGE)
        {
            char buf[30];
            GetWindowText(hEditIng, buf, 30);
            if (buf[0])
            {
                fridgeRoot = FridgeInsert(fridgeRoot, buf);
                SendMessage(hListFridge, LB_RESETCONTENT, 0, 0);
                FridgeDisplay(fridgeRoot, hListFridge);
                SetWindowText(hEditIng, "");
                char s[72];
                sprintf(s, "  Added \"%s\" to fridge.", buf);
                SetStatus(s);
            }
            else
            {
                SetStatus("  Type an ingredient name first.");
            }
        }
        /* Show all recipes */
        else if (id == ID_CHECK)
        {
            SendMessage(hListRecipes, LB_RESETCONTENT, 0, 0);
            Recipe *r = masterList;
            while (r)
            {
                SendMessage(hListRecipes, LB_ADDSTRING, 0, (LPARAM)r->title);
                r = r->next;
            }
            SetStatus("  All 14 recipes loaded. Select one to view details.");
        }
        /* Recipe selected from list */
        else if (id == ID_LB_RECIPES && notif == LBN_SELCHANGE)
        {
            int idx = SendMessage(hListRecipes, LB_GETCURSEL, 0, 0);
            char sel[50];
            SendMessage(hListRecipes, LB_GETTEXT, idx, (LPARAM)sel);

            PushHistory(sel);
            SendMessage(hListHist, LB_RESETCONTENT, 0, 0);
            for (int i = histTop; i >= 0; i--)
                SendMessage(hListHist, LB_ADDSTRING, 0, (LPARAM)histStack[i]);

            Recipe *r = masterList;
            while (r)
            {
                if (stricmp(r->title, sel) == 0)
                {
                    char det[700];
                    sprintf(det,
                            "Dish  : %s\r\n"
                            "Time  : %d minutes\r\n"
                            "Skill : %s\r\n"
                            "Tools : %s\r\n"
                            "\r\nIngredients\r\n"
                            "-----------------------------\r\n",
                            r->title, r->prepTime, r->skillLevel, r->utensils);
                    Ingredient *g = r->ingredients;
                    while (g)
                    {
                        char line[60];
                        sprintf(line, " [%s] %s\r\n",
                                FridgeSearch(fridgeRoot, g->name) ? "have" : "need",
                                g->name);
                        strcat(det, line);
                        g = g->next;
                    }
                    SetWindowText(hEditDetails, det);
                    char s[160];
                    sprintf(s, "  Viewing: %s  (%d min | %s)", r->title, r->prepTime, r->skillLevel);
                    SetStatus(s);
                    break;
                }
                r = r->next;
            }
        }
        /* Add to priority queue */
        else if (id == ID_ADD_QUEUE)
        {
            int idx = SendMessage(hListRecipes, LB_GETCURSEL, 0, 0);
            if (idx == LB_ERR)
            {
                SetStatus("  Select a recipe first.");
                break;
            }
            char sel[50];
            SendMessage(hListRecipes, LB_GETTEXT, idx, (LPARAM)sel);
            Recipe *r = masterList;
            while (r && stricmp(r->title, sel) != 0)
                r = r->next;
            if (r)
            {
                if (Enqueue(r->title, r->prepTime))
                {
                    QueueRefresh();
                    char s[128];
                    sprintf(s, "  \"%s\" added to cooking queue.", r->title);
                    SetStatus(s);
                }
                else
                {
                    SetStatus("  Already in the cooking queue.");
                }
            }
        }
        /* Add missing ingredients to shopping list */
        else if (id == ID_ADD_SHOP)
        {
            int idx = SendMessage(hListRecipes, LB_GETCURSEL, 0, 0);
            if (idx == LB_ERR)
            {
                SetStatus("  Select a recipe first.");
                break;
            }
            char sel[50];
            SendMessage(hListRecipes, LB_GETTEXT, idx, (LPARAM)sel);
            Recipe *r = masterList;
            while (r && stricmp(r->title, sel) != 0)
                r = r->next;
            if (r)
            {
                int added = 0;
                Ingredient *g = r->ingredients;
                while (g)
                {
                    if (!FridgeSearch(fridgeRoot, g->name))
                        if (AddToShop(g->name))
                        {
                            SendMessage(hListShop, LB_ADDSTRING, 0, (LPARAM)g->name);
                            added++;
                        }
                    g = g->next;
                }
                char s[160];
                if (added)
                    sprintf(s, "  Added %d missing item(s) to shopping list.", added);
                else
                    sprintf(s, "  Great! You already have everything for %s.", r->title);
                SetStatus(s);
            }
        }
        /* Remove selected item from queue (NEW) */
        else if (id == ID_REM_QUEUE)
        {
            int idx = SendMessage(hListQueue, LB_GETCURSEL, 0, 0);
            if (idx == LB_ERR)
            {
                SetStatus("  Select a queue item to remove.");
                break;
            }
            QueueNode *cur = qFront, *prev = NULL;
            int i = 0;
            while (cur && i < idx)
            {
                prev = cur;
                cur = cur->next;
                i++;
            }
            if (cur)
            {
                if (!prev)
                    qFront = cur->next;
                else
                    prev->next = cur->next;
                char s[80];
                sprintf(s, "  Removed \"%s\" from queue.", cur->title);
                free(cur);
                QueueRefresh();
                SetStatus(s);
            }
        }
        /* Clear shopping list (NEW) */
        else if (id == ID_CLEAR_SHOP)
        {
            ShopClear();
        }
        /* Cycle featured recipe */
        else if (id == ID_NEXT_FEAT)
        {
            if (featCurr)
            {
                char title[50];
                strcpy(title, featCurr->title);
                SetWindowText(hLblFeat, title);
                featCurr = featCurr->next;
                char s[80];
                sprintf(s, "  Featured: %s", title);
                SetStatus(s);
            }
        }
        break;
    }

    /* ── BACKGROUND / PANELS ───────────────────────────────── */
    case WM_ERASEBKGND:
    {
        HDC dc = (HDC)wp;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, hBrBg); /* fill entire BG dark */

        /* Top row panels */
        PanelDraw(dc, 15, 33, 218, 302);  /* Fridge               */
        PanelDraw(dc, 241, 33, 222, 302); /* Recipes              */
        PanelDraw(dc, 471, 33, 474, 302); /* Details              */

        /* Bottom row panels */
        PanelDraw(dc, 15, 333, 288, 218);  /* Cooking Queue        */
        PanelDraw(dc, 311, 333, 222, 218); /* Shopping List        */
        PanelDraw(dc, 541, 333, 200, 218); /* View History         */
        PanelDraw(dc, 749, 333, 213, 218); /* Featured             */

        return 1; /* suppress default background erase */
    }

    /* ── CONTROL COLOURS ───────────────────────────────────── */
    case WM_CTLCOLORLISTBOX:
    {
        HDC dc = (HDC)wp;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_LIST);
        return (LRESULT)hBrList;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wp;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_LIST);
        return (LRESULT)hBrList;
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wp;
        HWND ctrl = (HWND)lp;
        SetBkMode(dc, TRANSPARENT);
        if (ctrl == hLblFeat)
            SetTextColor(dc, C_GOLD);
        else
            SetTextColor(dc, C_TEXT);
        return (LRESULT)hBrPanel; /* panel bg shows through text */
    }
    case WM_CTLCOLORBTN:
        return (LRESULT)hBrBg;

    /* ── OWNER-DRAW BUTTONS ────────────────────────────────── */
    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlType != ODT_BUTTON)
            break;

        HDC dc = di->hDC;
        RECT rc = di->rcItem;
        BOOL pressed = (di->itemState & ODS_SELECTED) != 0;
        BOOL hot = (di->itemState & ODS_HOTLIGHT) != 0;

        /* Background */
        COLORREF bg = pressed ? C_BTNSEL : (hot ? C_BTNHOV : C_BTNBG);
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(dc, &rc, br);
        DeleteObject(br);

        /* Gold 1-px border */
        HPEN pen = CreatePen(PS_SOLID, 1, C_GOLD);
        HPEN opn = (HPEN)SelectObject(dc, pen);
        HBRUSH onb = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(dc, opn);
        SelectObject(dc, onb);
        DeleteObject(pen);

        /* Centred label text */
        char text[128];
        GetWindowText(di->hwndItem, text, 128);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, pressed ? C_TEXT : C_GOLD);
        HFONT of = (HFONT)SelectObject(dc, hFUI);
        DrawText(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);

        if (di->itemState & ODS_FOCUS)
            DrawFocusRect(dc, &rc);
        return TRUE;
    }

    /* ── RESIZE ────────────────────────────────────────────── */
    case WM_SIZE:
        if (hStatus)
            SendMessage(hStatus, WM_SIZE, wp, lp);
        break;

    /* ── CLEANUP ───────────────────────────────────────────── */
    case WM_DESTROY:
    {
        /* BUG FIX: original checked fp AFTER writing (crash if NULL) */
        FILE *fp = fopen("fridge.txt", "w");
        if (fp)
        {
            FridgeSaveDFS(fridgeRoot, fp);
            fclose(fp);
        }

        /* Release GDI resources */
        DeleteObject(hFUI);
        DeleteObject(hFBold);
        DeleteObject(hBrBg);
        DeleteObject(hBrPanel);
        DeleteObject(hBrList);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

/* ════════════════════════════════════════════════════════════
   ENTRY POINT
════════════════════════════════════════════════════════════ */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR args, int nCmdShow)
{
    (void)hPrev;
    (void)args; /* suppress -Wunused-parameter */
    /* Init Common Controls for status bar */
    INITCOMMONCONTROLSEX icex = {sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icex);

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hInstance = hInst;
    wc.lpszClassName = "SmartChefPro";
    wc.lpfnWndProc = WndProc;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassEx(&wc))
        return -1;

    HWND hwnd = CreateWindowEx(0,
                               "SmartChefPro",
                               "Smart Chef  -  Ultimate Edition",
                               WS_OVERLAPPEDWINDOW,
                               80, 40, 980, 660,
                               NULL, NULL, hInst, NULL);
    if (!hwnd)
        return -1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}