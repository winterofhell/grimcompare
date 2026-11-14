---
title: "Plague Garden — Acid/Poison DoT Conjurer (1.3.1.1)"
tags:
  - games/grim-dawn
  - builds
  - conjurer
  - dot
  - acid-poison
  - fangs-of-asterkarn
aliases:
  - Worldkeeper Conjurer
  - Чумной Сад Conjurer
class: "Conjurer (Occultist + Shaman)"
patch: "1.3.1.1 (FoA)"
playstyle: "Pure DoT / Kite & Facetank Hybrid"
---

# Plague Garden — Acid/Poison DoT Conjurer (`1.3.1.1`)

> [!abstract] Суть билда
> Мульти-DoT кастер на щите с **7–8 независимыми стакающимися источниками Poison DoT**, тремя слоями снижения резистов (RR) и живым «садом» из **10 призываемых ядовитых цветов** (*Living Blooms* + *Living Blossoms*), которые скейлятся от твоих статов.
> - **Референс эндгейма (HC 1.3.1.0):** *The Wild Nature* (`https://www.grimtools.com/calc/2ELAe4jN`) — SR 30–31 (`3:53`), Ravager (`0:31`), Callagadra (`0:34`).
> - **Кнопки:** 4–5 (спокойная ротация без пианино).

---

## 1. Механика урона и снижения резистов (RR)

### Независимые источники Poison DoT (все стакаются друг с другом)
1. **`Bloody Pox`** (`Occultist`) — вирус, сам прыгает по толпе (`4s` длительность, `+580 Poison/s` от сета + медали).
2. **`Grasping Vines`** (`Shaman`) — зона под ногами с КД `1s` (урон удара каждые `0.5s` лечит через `Mythical Bramblevine` и спавнит цветы).
3. **`Dreeg's Evil Eye` (`Focused Gaze`)** (`Occultist`) — КД-нюк по боссу (`2s` КД с `Mythical Bramblevine` и `15% ADCtH`).
4. **`Conjure Living Bloom`** (шлем, до `4` шт.) — `200 Acid` + `1000 Poison/5s`.
5. **`Conjure Living Blossom`** (4/4 сета, до `6` шт.) — `200–350 Acid` + `1500 Poison/5s`.
6. **`Scorpion's Sting`** (`Akeron's Scorpion`) — прок ядом + `-DA`.
7. **`Tainted Eruption`** (`Abomination`) — ядовитый взрыв `10m` + сильный `Poison DoT`.
8. **Проки экипировки:** `Awakened Ring of Eternal Rot` + `Noxious Fumes` (`Dung Divider`).

> [!warning] Важное правило наложения DoT
> DoT **запоминает RR и криты в момент наложения**. Всегда сначала кидай **`Curse of Frailty`** (срезает резисты и вешает `Rumor`), и только потом — `Grasping Vines`, `Dreeg's Evil Eye` и `Bloody Pox`.

### Все 3 категории Resistance Reduction (RR) — без дублей
| Категория RR | Источники в билде | Примечание |
| :--- | :--- | :--- |
| **1. `-X%` (складывается полностью)** | `Vulnerability` (`Curse of Frailty`) + `Rumor` (`Murmur`: `-30%`) + `Widow's Bite` + *Ascended Affixes* (`Curse of Frailty` / `Wind Devil`) | Главный слой. `Murmur` даёт почти треть всего среза резистов! |
| **2. `% Reduced` (мультипликативный)** | `Living Bloom` (`26% Reduced`) · `Awakened Ring of Eternal Rot` | Не берёт самый сильный — работает автоматически с цветов |
| **3. `n Reduced` (плоский)** | `Living Blossom` (`28 Reduced for 5s`) | Перекрывает `Manticore` (`Acid Spray`), поэтому в эндгейме **`Manticore` не нужна** |

---

## 2. Атрибуты и Умения (Skills)

### Атрибуты (Attributes)
* **`Spirit` / `Cunning`:** ровно столько, сколько требуют `Dung Divider` (`396 Cunning`, `495 Spirit` — `Crane` снижает требование Духа на оружие на `10%`) и оффхенд/бижа.
* **`Physique`:** всё остальное (нужно под тяжёлую броню `Worldkeeper's Garden` и запас HP/DA).

### Приоритет умений — Occultist (Mastery `50/50`)

| Скилл | Очки (база) | Роль и заметки |
| :--- | :--- | :--- |
| **`Curse of Frailty`** | `10/10` | Замедление, огромный радиус, прок для `Rumor` |
| **`Vulnerability`** | `10/10` (хардкап!) | Главный `-X% Acid/Poison RR` + срез DA |
| **`Bloody Pox`** | `1–12/12` | 1–12 оч. (не перекачивай рано из-за маны; сет даёт скидки на манакост) |
| **`Wasting`** | `12/12` | Срезает OA врагов (критично против боссов) + урон |
| **`Black Death`** | `9–12/12` | Основной урон Ядом у `Bloody Pox` (сет даёт `+3`, добери до хардкапа `22/12` если влезает) |
| **`Fevered Rage`** | **`0/1` (НЕ БРАТЬ!)** | Ускоряет боссов на `+150% Total Speed` — на эндгейме/HC это смерть |
| **`Dreeg's Evil Eye`** | `16/16` (или `1` если без него) | Соло-цель урон; бери трансмутер **`Focused Gaze`** (`1/1`) |
| **`Blood Burst` / `Terrifying Gaze`** | `1` / `1–12` | Зависит от свободных очков; `Focused Gaze` + `DEE` важнее |
| **`Blood of Dreeg`** | `16/16` | Кнопка хила, OA, реген HP (юзать по КД) |
| **`Aspect of the Guardian`** | `12/12` | `Physical Resistance` + `Poison/Acid %` + резист к Яду |
| **`Possession`** | `12/12` (**обязательно**) | Эксклюзив: огромный `% Acid/Poison`, **Damage Absorption**, защита от срыва кастов |

### Приоритет умений — Shaman (Mastery `32/50` или `50/50`)

| Скилл | Очки (база) | Роль и заметки |
| :--- | :--- | :--- |
| **`Grasping Vines`** | `16/16` | Основной спам под ноги (КД `1s` от сета), лечит, спавнит цветы |
| **`Entangling Vines`** | `12/12` | Повышает урон и контроль толпы (сет даёт `+3`) |
| **`Mogdrogen's Pact`** | `1–12` | Реген энергии и базовый реген HP |
| **`Heart of the Wild`** | `10/10` | Огромный бонус к `% Health` и `% Health Regeneration` |
| **`Oak Skin`** | `10/10` | Броня, Pierce-резист, реген HP (в 1.3% с `Worldkeeper` реген разгоняется в космос) |
| **`Wendigo Totem`** | `1–12/12` | Процентный хил под ногами в файте с боссом (`Blood Pact` **не брать**) |
| **`Brute Force` / `Primal Bond`** | **`0` (НЕ БРАТЬ!)** | `Brute Force` работает ТОЛЬКО с двуручкой, а `Primal Bond` не даёт Acid/Poison |

*(Опционально для мин-макса в FoA с Ascended-аффиксами: если выбил шмот с `-Acid RR` на **`Wind Devil`**, вложи `1` в `Wind Devil` + `1` в `Raging Tempest` для ещё одного источника `-X%` RR.)*

---

## 3. Созвездия (Devotions) — точный маршрут `55/55`

> [!info] Окончательные цифры маршрута (проверено по Wiki 2026)
> * **Всего очков:** `55/55`
> * **Итоговые аффинити:** `Chaos 9` · `Eldritch 23` · `Primordial 6` · `Order 5`
> * **Защита от отражения (Reflect Damage Reduction):** **`25%`** с созвездий (`Crane 15%` + `Yugol 10%`) + компонент `Titan Plating` в броню.
> * **Про холодный урон в `Murmur` и `Yugol`:** плоский Cold от `Black Blood of Yugol` не конвертится в Acid, но оба созвездия всё равно обязательны ради **`-30% Acid RR`**, **`+180% Acid`**, **`+50 OA`**, **`+300 HP`**, **`25% Vit Res`**, **`10% Reflect Red`**, **`8% ADCtH`** и **`-6%` урона врагов**.

### Пошаговый порядок прокачки (55/55)

1. **`Crossroads Primordial`** (`1`) → `P=1`
2. **`Sailor's Guide`** (`4`, нужно `P1`) → даёт `P5` → `P=6` *(Phys Res, Slow/Freeze Res, Move Speed)*
3. **`Crossroads Chaos`** (`1`) → `C=1, P=6`
4. **`Rat`** (`4`, нужно `C1`) → даёт `C2, E3` → `C=3, E=3, P=6` *(+30% Poison Duration, Poison Res)*
5. **`Akeron's Scorpion`** (`5`, нужно `E1`) → даёт `E5` → `C=3, E=8, P=6`
6. **`Spider`** (`5`, нужно `E1`) → даёт `E6` → `C=3, E=14, P=6` *(OA, DA, Cast Speed)*
7. **`Murmur, Mistress of Rumors`** (`6`, нужно `C3, E6, P6`) → даёт `C2, E2` → `C=5, E=16, P=6` *(**`-30%` Acid RR!**)*
8. **`Bat`** (`5`, нужно `E1`) → даёт `C2, E3` → `C=7, E=19, P=6` *(Vitality → Acid конвертится и хилит)*
9. **`Behemoth`** (`6`, нужно `C3, E4, P4`) → даёт `C2, E3` → `C=9, E=22, P=6` *(Giant's Blood + HP + Regen)*
10. **Сбросить `Crossroads Chaos`** (`-1`) → `C=8, E=22, P=6` *(все созвездия полностью поддерживают друг друга: внешние `Chaos = 6` для каждого из Rat/Murmur/Bat/Behemoth)*
11. **`Abomination`** — ядовитая ветка `1-2-3-4a-5a-6a` (`6`, нужно `C8, E18`) → `Tainted Eruption`
12. **`Yugol, the Insatiable Night`** (`6`, нужно `C7, E20`) → `Black Blood of Yugol`
13. **`Crossroads Order`** (`1`) → **`Crane`** (`5`, нужно `O1`, даёт `O5`) → **сбросить `Crossroads Order`** (`-1`) *(самоподдержка `O5 >= O1`! Даёт `15% Reflect Red`, резисты и `-10% Spirit Req`)*
14. **Последние `2` очка (выбор):**
    * **Вариант А (жир + OA):** `Crossroads Chaos` (`1`, `+5% Health`) + `Crossroads Eldritch` (`1`, `+18 OA`).
    * **Вариант Б (урон + отхил):** `Ghoul` 1-я звезда (`1`, `+4% ADCtH`) + `Jackal` 1-я звезда (`1`, `+15% All Damage`).

**Итоговый контроль очков:** `1 + 4 + 0 + 4 + 5 + 5 + 6 + 5 + 6 + 6 + 6 + 5 + 2 = 55/55`.

### Привязка проков (Celestial Powers)
* `Rumor` (`Murmur`) → **`Curse of Frailty`**
* `Scorpion's Sting` (`Akeron's Scorpion`) → **`Grasping Vines`**
* `Tainted Eruption` (`Abomination`) → **`Bloody Pox`**
* `Twin Fangs` (`Bat`) → **`Dreeg's Evil Eye`** *(или **`Wendigo Totem`**)*
* `Giant's Blood` (`Behemoth`) → **`Possession`** (или `Blood of Dreeg`)
* `Black Blood of Yugol` (`Yugol`) → **`Mogdrogen's Pact`** (срабатывает при получении удара)

> [!tip] Замена на прокачке `1–93` (пока нет сета Worldkeeper)
> До сборки 4/4 сета у тебя нет `Living Blossom`, поэтому временно бери **`Manticore`** (`Acid Spray` для плоского RR) вместо `Yugol`. На `94+` уровне, надев сет, сбрось `Manticore` в `Yugol` у Spirit Guide в Devil's Crossing.

---

## 4. Пошаговая прокачка `1 → 100`

* **Уровни `1–10` (старт Shaman):** начинай с **`Devouring Swarm`** (`16/16` к 10-му — аннигилирует весь 1-й акт) + `1` очко в **`Grasping Vines`**.
* **Уровни `10–35` (подключаем Occultist):** вложи 1 очко в `Curse of Frailty`, открой `Blood of Dreeg` и начни плавно качать `Bloody Pox` (`1–3` очка в базу, чтобы не проседать по мане, упор в `Black Death`).
  * В Акте 1 (Burrwitch Outskirts) можно выбить **`Groble Toxic Effigy`** (если хочешь добивать боссов через `Dreeg's Evil Eye`).
  * Сразу после Акта 1 заскочи на `10 минут` в пустыню Forgotten Gods и выбей оффхенд **`Groble Vile Effigy`** — даёт `Vitality → Acid` для `Bloody Pox` и `-10% Poison RR` с самых ранних уровней.
  * В Акте 2/3 продаётся/крафтится **`Rhowari Cord`** (`+1 Occultist`), в Улье — амулет **`Ronaprax's Sting`**.
* **Уровни `35–93`:** макси `Vulnerability`, `Possession`, `Grasping Vines` + `Entangling Vines`, `Heart of the Wild`, `Oak Skin`. Держи все резисты на `80%+`.
* **Уровень `94–100`:** скрафти шлем `Worldkeeper's Gaze` у кузнеца, добей 3 части сета через *Ascendant's Spoils* / фарм, выбей **`Dung Divider`** (секретный квест FoA, босс *Nicroak, First of the Chosen* за секретным проходом у *Beaver's Crossing*) и пересобери созвездия на финальный маршрут.

---

## 5. Финальная экипировка (BiS, `1.3.1.1`)

| Слот | Лучший предмет (BiS) | Бюджетная / альтернативная замена |
| :--- | :--- | :--- |
| **Сет (4 слота)** | **`Worldkeeper's Garden`** (*Gaze*, *Thicket*, *Vines*, *Heart*) | *Gaze* крафтится; плечи/броня/медаль — дроп и *Ascendant's Spoils* |
| **Оружие** | **`Dung Divider`** *(секрет FoA; 101 ур. → надевается в `100` с `Fewmet Grips`)* | **`Misery`** (чуть меньше защиты/AoE, но проще найти и не требует `Fewmet Grips`) |
| **Щит** | **`Mythical Bramblevine`** *(незаменим: `5% ADCtH` Vines + `15% ADCtH` DEE)* | На прокачке: `Groble Vile Effigy` / `Groble Toxic Effigy` |
| **Амулет** | **`Conduit of Wild Whispers`** *(Acid `Grasping Vines` вариант)* | **`Mythical Pestilence of Dreeg`** / **`Venomblade`** / **`Scarab Shell`** |
| **Кольцо 1** | **`Widow's Bite`** *(крафтовое FoA: `-Acid RR`)* | Любое кольцо с Acid/Poison, OA и резистами |
| **Кольцо 2** | **`Awakened Ring of Eternal Rot`** *(`+Vulnerability`, Poison DoT, `% RR`)* | **`Mythical Ring of the Black Matriarch`** / второй **`Widow's Bite`** |
| **Перчатки** | **`Fewmet Grips`** *(даёт `-1 Level Req` для `Dung Divider` + 2pc `+120% Acid` + DoT)* | **`Mythical Viperfang Grips`** / **`Dark One's Grasp`** (если играешь с `Misery`) |
| **Штаны** | **`Grava'Thul's Leggings`** *(`+Vulnerability`, DA shred, отличные резисты)* | **`Mythical Venomskin Legwraps`** / **`Bysmiel-Sect Legguards`** |
| **Ботинки** | **`Mythical Intrepid Warboots`** *(CC-резисты + защита)* | **`Stoneplate Greaves`** (крафт) · **`Mythical Vilescorn Greaves`** |
| **Пояс** | Пояс с `+1 Occultist/Shaman` и резистами *(напр., **`Mythical Girdle of Stolen Dreams`** / **`Mythical String of Maggots`** / **`Chains of Ygraad Brandis`**)* | **`Rhowari Cord`** (фракция) |
| **Реликвия** | **`Deathstalker`** *(доп. аура `-Acid RR`)* | **`Dreeg's Affliction`** · **`Malediction`** *(если хочешь DoT-прок)* |

### Приоритет Ascended Affixes (в эндгейме FoA)
1. **Оружие / Бижутерия:** `-Acid RR` к **`Curse of Frailty`** и **`Wind Devil`**.
2. **Оверкап и статы:** DA shred к `Vulnerability`, Crit Damage к `Blood of Dreeg`, % Absorption к `Possession`, OA и Movement Speed.

---

## 6. Ротация в бою
1. **Толпа (треш):** `Curse of Frailty` → `Grasping Vines` под ноги → `Bloody Pox` → бежишь дальше, пока цветы дожигают экран.
2. **Босс / Немезида / Супербосс:**
   * Кидаешь **`Curse of Frailty`** (срезал резисты и повесил `Rumor`).
   * Ставишь **`Wendigo Totem`** под себя.
   * Спамишь **`Grasping Vines`** каждую секунду (поддерживаешь 10 цветов и вампиризм).
   * По КД жмёшь **`Dreeg's Evil Eye`** (`Focused Gaze`, каждые `2s`) и **`Bloody Pox`** (раз в `4s`).
   * **`Blood of Dreeg`** прожимаешь по откату ради OA, защиты и лечения.