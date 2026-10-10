#pragma once
class ldomDocument;
namespace kobalt {
// Mark text-only body blocks without changing source text or anchor structure.
void markBodyAlignment(ldomDocument *document);
}
