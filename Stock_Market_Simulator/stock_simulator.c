#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define API_KEY "WIKYX2YRWGNTAA50"
#define MAX_HOLDINGS 20
#define SYMBOL_LEN 16

/* ================= DATA STRUCTURES ================= */

typedef struct
{
    char symbol[SYMBOL_LEN];
    int quantity;
    double avg_price;
} Holding;

typedef struct
{
    double cash;
    Holding holdings[MAX_HOLDINGS];
    int count;
} Account;

struct response
{
    char *data;
    size_t size;
};

/* ================= CURL CALLBACK ================= */

size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    struct response *res = userdata;

    char *temp = realloc(res->data, res->size + total + 1);
    if (!temp)
        return 0;

    res->data = temp;
    memcpy(res->data + res->size, ptr, total);
    res->size += total;
    res->data[res->size] = '\0';

    return total;
}

/* ================= FETCH PRICE ================= */

double fetch_price(const char *symbol)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;

    struct response res = {NULL, 0};
    char url[512];

    snprintf(url, sizeof(url),
             "https://www.alphavantage.co/query?"
             "function=GLOBAL_QUOTE&symbol=%s&apikey=%s",
             symbol, API_KEY);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || !res.data)
    {
        printf("Network error\n");
        free(res.data);
        return -1;
    }

    cJSON *root = cJSON_Parse(res.data);
    free(res.data);

    if (!root)
    {
        printf("JSON parse error\n");
        return -1;
    }

    cJSON *quote = cJSON_GetObjectItem(root, "Global Quote");
    if (!quote)
    {
        printf("Symbol not found\n");
        cJSON_Delete(root);
        return -1;
    }

    cJSON *price = cJSON_GetObjectItem(quote, "05. price");
    if (!price)
    {
        printf("Price unavailable\n");
        cJSON_Delete(root);
        return -1;
    }

    double value = atof(price->valuestring);
    cJSON_Delete(root);
    return value;
}

/* ================= PORTFOLIO LOGIC ================= */

int find_holding(Account *acc, const char *symbol)
{
    for (int i = 0; i < acc->count; i++)
    {
        if (strcmp(acc->holdings[i].symbol, symbol) == 0)
            return i;
    }
    return -1;
}

void buy_stock(Account *acc, const char *symbol, int qty, double price)
{
    double cost = qty * price;

    if (acc->cash < cost)
    {
        printf("Insufficient balance\n");
        return;
    }

    int idx = find_holding(acc, symbol);

    if (idx == -1)
    {
        strcpy(acc->holdings[acc->count].symbol, symbol);
        acc->holdings[acc->count].quantity = qty;
        acc->holdings[acc->count].avg_price = price;
        acc->count++;
    }
    else
    {
        Holding *h = &acc->holdings[idx];
        double total = h->avg_price * h->quantity + cost;
        h->quantity += qty;
        h->avg_price = total / h->quantity;
    }

    acc->cash -= cost;
    printf("Bought %d %s @ $%.2f\n", qty, symbol, price);
}

void sell_stock(Account *acc, const char *symbol, int qty, double price)
{
    int idx = find_holding(acc, symbol);

    if (idx == -1 || acc->holdings[idx].quantity < qty)
    {
        printf("Not enough shares\n");
        return;
    }

    acc->holdings[idx].quantity -= qty;
    acc->cash += qty * price;

    printf("Sold %d %s @ $%.2f\n", qty, symbol, price);

    if (acc->holdings[idx].quantity == 0)
    {
        acc->holdings[idx] = acc->holdings[acc->count - 1];
        acc->count--;
    }
}

void show_portfolio(Account *acc)
{
    printf("\nPORTFOLIO\n");
    printf("----------------------------------\n");

    for (int i = 0; i < acc->count; i++)
    {
        double curr = fetch_price(acc->holdings[i].symbol);
        double pnl = (curr - acc->holdings[i].avg_price) * acc->holdings[i].quantity;

        printf("%s | Qty: %d | Avg: $%.2f | LTP: $%.2f | PnL: $%.2f\n",
               acc->holdings[i].symbol,
               acc->holdings[i].quantity,
               acc->holdings[i].avg_price,
               curr,
               pnl);
    }
}

/* ================= MAIN CLI ================= */

int main()
{
    Account acc = {.cash = 100000.0, .count = 0};
    char line[128];

    curl_global_init(CURL_GLOBAL_ALL);

    printf("Stock Market Simulator (C)\n");
    printf("Starting Balance: $%.2f\n", acc.cash);
    printf("Commands: price <SYM>, buy <SYM> <QTY>, sell <SYM> <QTY>, portfolio, balance, exit\n");

    while (1)
    {
        printf("\n> ");
        if (!fgets(line, sizeof(line), stdin))
            break;

        char *cmd = strtok(line, " \n");
        if (!cmd)
            continue;

        if (strcmp(cmd, "price") == 0)
        {
            char *sym = strtok(NULL, " \n");
            if (!sym)
                continue;
            double p = fetch_price(sym);
            if (p > 0)
                printf("%s price: $%.2f\n", sym, p);
        }

        else if (strcmp(cmd, "buy") == 0)
        {
            char *sym = strtok(NULL, " \n");
            char *q = strtok(NULL, " \n");
            if (!sym || !q)
                continue;
            double p = fetch_price(sym);
            if (p > 0)
                buy_stock(&acc, sym, atoi(q), p);
        }

        else if (strcmp(cmd, "sell") == 0)
        {
            char *sym = strtok(NULL, " \n");
            char *q = strtok(NULL, " \n");
            if (!sym || !q)
                continue;
            double p = fetch_price(sym);
            if (p > 0)
                sell_stock(&acc, sym, atoi(q), p);
        }

        else if (strcmp(cmd, "portfolio") == 0)
        {
            show_portfolio(&acc);
        }

        else if (strcmp(cmd, "balance") == 0)
        {
            printf("Balance: $%.2f\n", acc.cash);
        }

        else if (strcmp(cmd, "exit") == 0)
        {
            break;
        }

        else
        {
            printf("Unknown command\n");
        }
    }

    curl_global_cleanup();
    return 0;
}
