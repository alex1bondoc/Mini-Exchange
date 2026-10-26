/*
SESIUNE FIX — reguli

Stări:
  AwaitingLogon   conexiune nouă, încă fără Logon valid
  Active          logat, mesajele de ordine trec spre engine
  LoggedOut       finală: s-a trimis Logout sau s-a închis; orice mesaj e ignorat,
                  gateway-ul închide conexiunea. Reconectare = Session nou, secvențe de la 1.

Rezultate:
  închis          se taie conexiunea fără să se trimită nimic
  logout          se trimite 35=5 (cu 58=motiv), apoi se închide → LoggedOut
  reject(3, r)    se trimite 35=3 cu 45=secvența mesajului, 373=r, 58=motiv; sesiunea continuă
  răspunde X      se trimite mesajul X; sesiunea continuă
  livrează        mesajul merge la convert → engine
  nimic           nu se trimite nimic

Ordinea verificărilor în Active, pentru orice mesaj primit:
  1. 34 lipsă sau nenumeric           → logout   (fără 34 nu mai poți număra secvențele)
  2. 49 / 56 lipsă sau greșite        → logout   (49 = cel din Logon, 56 = CompID-ul exchange-ului)
  3. 34 != secvența așteptată          → logout   (fără ResendRequest: orice gol sau repetare închide)
  4. abia acum: secvența așteptată++, last_received = now, apoi tratezi tipul

Logon valid = 34=1, 98=0, 108 > 0, 49 prezent, 56 = CompID-ul exchange-ului.

373 SessionRejectReason folosite: 1 tag obligatoriu lipsă, 5 valoare incorectă,
                                  6 format incorect, 11 MsgType invalid.

Fiecare mesaj trimis: secvența de ieșire++ (pornește de la 1), last_sent = now.

┌──────────────────────┬────────────────────────┬──────────────────────────────┬───────────┐
│ mesaj primit         │ AwaitingLogon          │ Active                       │ LoggedOut │
├──────────────────────┼────────────────────────┼──────────────────────────────┼───────────┤
│ A Logon valid        │ răspunde A (98=0,      │ logout (Logon dublu)         │ nimic     │
│                      │ 108 același) → Active  │                              │           │
│ A Logon invalid      │ logout                 │ logout                       │ nimic     │
│ 0 Heartbeat          │ închis                 │ nimic                        │ nimic     │
│ 5 Logout             │ închis                 │ răspunde 5 → LoggedOut,      │ nimic     │
│                      │                        │ închis                       │           │
│ D/F/G valide         │ închis                 │ livrează                     │ nimic     │
│ D/F/G pe care        │ închis                 │ reject(3, 1/5/6)             │ nimic     │
│ convert le respinge  │                        │ (secvența rămâne consumată)  │           │
│ 3 Reject de la client│ închis                 │ nimic (doar log)             │ nimic     │
│ alt tip (1,2,4,8,9,  │ închis                 │ reject(3, 11)                │ nimic     │
│ V, H, ...)           │                        │                              │           │
│ 34 / 49 / 56 lipsă   │ închis                 │ logout                       │ nimic     │
│ sau greșite          │                        │                              │           │
│ secvență greșită     │ (prinsă la Logon       │ logout                       │ nimic     │
│                      │ invalid: 34 != 1)      │                              │           │
├──────────────────────┼────────────────────────┼──────────────────────────────┼───────────┤
│ on_timer(now)        │ fără Logon în          │ now - last_sent >= HeartBtInt│ nimic     │
│                      │ LOGON_TIMEOUT de la    │   → răspunde 0               │           │
│                      │ conectare → închis     │ now - last_received >        │           │
│                      │                        │ 1.5 × HeartBtInt → logout    │           │
└──────────────────────┴────────────────────────┴──────────────────────────────┴───────────┘
*/

// TODO Session — după ce trec testele din SessionMessagesTests.cpp
//
//  1. Hotărăște ce primește constructorul: CompID-ul exchange-ului, LOGON_TIMEOUT,
//     momentul conectării (pentru timeout-ul de logon). Session NU citește ceasul singur.
//
//  2. Membrii: starea, CompID-ul clientului (COPIAT, nu string_view — de ce?),
//     secvența așteptată la intrare, secvența următoare la ieșire, HeartBtInt,
//     last_received, last_sent.
//
//  3. Ce întoarce Session gateway-ului (SessionResult)? Pornește de la coloana „Rezultate”
//     din tabel: ce informații îi trebuie gateway-ului ca să construiască singur fiecare răspuns
//     (tip, secvența de referință pentru 3, motiv 373, text pentru 58)?
//
//  4. Metodele:
//       on_message(const Message&, Timestamp now)  → tabelul de sus, rând cu rând
//       on_timer(Timestamp now)                    → rândul on_timer
//       next_outgoing_seq() / on_sent(Timestamp)   → cine actualizează last_sent?
//       o metodă prin care gateway-ul cere un reject(3) când convert respinge un D/F/G
//         (Session a acceptat deja mesajul; ce secvență pune în 45?)
//
//  5. Teste în tests/SessionTests.cpp, construind FIX::Message direct (fără parser), câte unul
//     pentru fiecare celulă din tabel. Începe cu: Logon valid, D înainte de Logon, secvență sărită.
//
//  Verifică-te: pentru fiecare celulă din tabel poți spune DE CE e acel rezultat și nu altul?

