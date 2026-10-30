import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from dialogue.anonymizer import scrub_text
from dialogue.output_parser import parse_reply
from dialogue.price_guard import stated_prices, validated_ad_price
from dialogue.prompt_builder import build_messages
from llm.base import ThinkStripper


def test_scrub_pii():
    t = scrub_text("Ben Ali, 0532 123 45 67, ali@x.com, TC 12345678901, https://a.b/c, 192.168.1.1")
    for leaked in ("0532", "ali@x.com", "12345678901", "https://", "192.168"):
        assert leaked not in t


def test_scrub_redact_terms():
    assert "Yusuf" not in scrub_text("Merhaba ben yusuf", ["Yusuf"])


def test_generated_ad_prices_must_be_explicitly_stated():
    assert stated_prices(["İkinci el masa ilanı, fiyatı belirtilmedi."]) == {}
    assert validated_ad_price("$450", {}) == ""

    prices = stated_prices(["İlan: masa, fiyat: $450."])
    assert prices == {"450": "$450"}
    assert validated_ad_price("$450", prices) == "$450"
    assert validated_ad_price("$900", prices) == ""


def test_prompt_has_no_player_profile_or_exact_cash():
    state = {"items": [{"id": "a", "name": "A", "price": 100}], "player_cash": 123456, "cheapest_price": 100,
             "rapport": 70, "player_profile": {"hometown": "Gizli"}}
    npc = {"personality": "x", "profile": {"name": "N"}}
    sysmsg = build_messages("shop", npc, state, [], "selam")[0]["content"]
    assert "123456" not in sysmsg and "Gizli" not in sysmsg and "player_can_afford_any" in sysmsg
    assert "fiyat ya da ücret uydurma" in sysmsg


def test_parse_fenced_and_trailing_comma():
    raw = '```json\n{"say": "Buyur", "intent": "offer", "player_offer": 90.0,}\n```'
    r = parse_reply(raw, "shop")
    assert r["say"] == "Buyur" and r["intent"] == "offer" and r["player_offer"] == 90


def test_parse_unknown_intent_and_garbage():
    assert parse_reply('{"say": "x", "intent": "flee"}', "shop")["intent"] == "chat"
    assert parse_reply("bozuk çıktı", "shop")["say"] == "Pardon, anlamadım?"


def test_parse_ask_directions_keeps_destination():
    r = parse_reply('{"say": "Bir bakayim", "intent": "ask_directions", "destination": "havaalani"}', "street")
    assert r["intent"] == "ask_directions" and r["destination"] == "havaalani"
    # shop bağlamında bu intent tanınmaz
    assert parse_reply('{"say": "x", "intent": "ask_directions"}', "shop")["intent"] == "chat"


def test_taxi_heli_phone_intents():
    assert parse_reply('{"say": "x", "intent": "set_destination", "destination": "havaalani"}', "taxi")["destination"] == "havaalani"
    assert parse_reply('{"say": "x", "intent": "set_destination"}', "heli")["intent"] == "set_destination"
    for i in ("call_taxi", "call_heli_taxi", "call_police", "call_ambulance", "call_fire", "hang_up"):
        assert parse_reply('{"say": "x", "intent": "%s"}' % i, "phone")["intent"] == i
    r = parse_reply('{"say":"Arıyorum","intent":"call_contact","contact_id":"person:m1"}', "phone")
    assert r["intent"] == "call_contact" and r["contact_id"] == "person:m1"
    # telefonda taksi intent'i tanınır, sokakta tanınmaz
    assert parse_reply('{"say": "x", "intent": "call_taxi"}', "street")["intent"] == "chat"


def test_random_contact_call_and_hangout_intents():
    assert parse_reply('{"say": "Tabii, arabanın yanında buluşalım.", "intent": "invite"}', "person")["intent"] == "invite"
    assert parse_reply('{"say": "Evine geleyim.", "intent": "invite_home"}', "person")["intent"] == "invite_home"
    assert parse_reply('{"say": "Evine geleyim.", "intent": "invite_home"}', "quest")["intent"] == "invite_home"
    assert parse_reply('{"say": "Sonra görüşürüz.", "intent": "hang_up"}', "person")["intent"] == "hang_up"
    assert parse_reply('{"say": "Selam", "intent": "invite"}', "street")["intent"] == "chat"
    npc = {"personality": "Samimi ve neşeli", "profile": {"name": "N"}}
    person_prompt = build_messages("person", npc, {}, [], "Benimle buluşur musun?")[0]["content"]
    assert "daha önce sokakta tanıştığın" in person_prompt and "intent 'invite'" in person_prompt
    home_state = {"can_invite_to_home": True}
    home_prompt = build_messages("person", npc, home_state, [], "Evime gel")[0]["content"]
    assert "can_invite_to_home" in home_prompt and "invite_home" in home_prompt
    assert "sana uygun bir buluşma yeri bulayım" in person_prompt
    assert "'buradan hareket edemiyorum' deme" in person_prompt
    assert parse_reply('{"say": "Yol güzelmiş.", "intent": "chat"}', "hangout")["intent"] == "chat"


def test_haviland_can_access_saved_contacts():
    npc = {"personality": "Telefon yapay zekâ asistanısın.", "profile": {"name": "Haviland"}}
    contacts = [{"id": "person:m1", "name": "Ayşe"}]
    prompt = build_messages("phone", npc, {"contacts": contacts}, [], "Ayşe'yi ara")[0]["content"]
    assert '"contacts": [{"id": "person:m1", "name": "Ayşe"}]' in prompt
    assert "call_contact" in prompt and "contact_id" in prompt


def test_currency_is_always_dollars():
    from dialogue.output_parser import normalize_currency
    cases = {
        "Sultan 12.000 TL kardeşim": "Sultan 12.000 dolar kardeşim",
        "Ücret ₺500": "Ücret 500 dolar",
        "Toplam $12.500 eder": "Toplam 12.500 dolar eder",
        "100 tl'ye veririm": "100 dolar veririm",
        "Elli lira fazla": "Elli dolar fazla",
        "Üç bin liralık iş": "Üç bin dolar iş",
        "Fiyat 8.000 dolar": "Fiyat 8.000 dolar",  # zaten doğru: dokunulmaz
        "Hotel tutuldu": "Hotel tutuldu",             # 'tl' kelime içinde: dokunulmaz
    }
    for src, want in cases.items():
        assert normalize_currency(src)[0] == want, (src, normalize_currency(src)[0])
    # parse_reply de uygular
    r = parse_reply('{"say": "Bu araç 8000 TL", "intent": "chat"}', "shop")
    assert r["say"] == "Bu araç 8000 dolar"


def test_explicit_guard_and_parenthetical_note():
    from dialogue.output_parser import guard_explicit
    # normal romantik iltifat dokunulmaz
    ok = "Seninle olmak çok güzel, bugün çok şıksın."
    assert guard_explicit(ok, "partner") == (ok, False)
    # açık ifade: sevgilide ima edilen (Özel ilişki) + iltifat, diğerlerinde nazik sınır
    bad = "Hadi sevişelim"
    s, blocked = guard_explicit(bad, "partner")
    assert blocked and s.startswith("(Özel ilişki)")
    s, blocked = guard_explicit(bad, "friend")
    assert blocked and "(Özel ilişki)" not in s
    # parse_reply de ilişkiyi kullanır
    r = parse_reply('{"say": "seks yapalım", "intent": "chat"}', "quest", "partner")
    assert r["say"].startswith("(Özel ilişki)")


def test_quest_prompt_carries_relationship_rules():
    npc = {"personality": "x", "profile": {"name": "N"}}
    sysmsg = build_messages("quest", npc, {"mission": {"relationship": "friend", "your_turn": True}}, [], "selam")[0]["content"]
    assert "(Özel ilişki)" in sysmsg and "friend" in sysmsg and "partner" in sysmsg


def test_player_text_tl_is_sent_to_llm_as_dollars():
    npc = {"personality": "x", "profile": {"name": "N"}}
    hist = [{"role": "user", "content": "10 bin TL olur mu"}, {"role": "assistant", "content": "Tamam"}]
    msgs = build_messages("shop", npc, {}, hist, "500 TL'ye veririm", scrub=True)
    joined = " ".join(m["content"] for m in msgs[1:])
    assert "TL" not in joined and "10 bin dolar" in joined and "500 dolar" in joined
    local = build_messages("shop", npc, {}, [], "9.000 lira", scrub=False)  # yerel provider da aynı
    assert local[-1]["content"] == "9.000 dolar"


def test_speed_request_intents_and_pilot_refusal_rule():
    for ctx in ("taxi", "heli"):
        for i in ("drive_faster", "drive_slower"):
            assert parse_reply('{"say": "x", "intent": "%s"}' % i, ctx)["intent"] == i
    # sokakta bu intent tanınmaz
    assert parse_reply('{"say": "x", "intent": "drive_faster"}', "street")["intent"] == "chat"
    npc = {"personality": "x", "profile": {"name": "N"}}
    heli = build_messages("heli", npc, {}, [], "selam")[0]["content"]
    taxi = build_messages("taxi", npc, {}, [], "selam")[0]["content"]
    assert "her zaman reddedilir" in heli and "her zaman reddedilir" not in taxi


def test_lawyer_prompt_uses_case_and_never_leaks_cash():
    state = {"case": {"reasons": ["Silah kullanılmadı."], "lawyer_fee": 1500}, "player_cash": 99887, "cheapest_price": 1500}
    npc = {"personality": "x", "profile": {"name": "N"}}
    sysmsg = build_messages("lawyer", npc, state, [], "selam")[0]["content"]
    assert "Silah kullanılmadı" in sysmsg and "99887" not in sysmsg and "player_can_afford_any" in sysmsg


def test_wanted_lawyer_intents_and_rules():
    for ctx in ("wanted_sule", "wanted_bar"):
        assert parse_reply('{"say": "Bir deneyeyim.", "intent": "clear_wanted"}', ctx)["intent"] == "clear_wanted"
        assert parse_reply('{"say": "Görüşürüz.", "intent": "hang_up"}', ctx)["intent"] == "hang_up"
        assert parse_reply('{"say": "Tamam.", "intent": "hire_lawyer"}', ctx)["intent"] == "chat"
    npc = {"personality": "Avukat", "profile": {"name": "Şule"}}
    for ctx in ("wanted_sule", "wanted_bar"):
        prompt = build_messages(ctx, npc, {}, [], "Aranmamı sildirebilir misin?")[0]["content"]
        assert "suçun türünü" in prompt
        assert "clear_wanted" in prompt
    case = {"offense": "Araç hırsızlığı: park halindeki araç çalındı", "wanted_stars": 2}
    prompt = build_messages("wanted_sule", npc, {"case": case}, [], "Dosyayı sil")[0]["content"]
    assert case["offense"] in prompt and "suçun türünü" in prompt


def test_think_stripper_across_chunks():
    s = ThinkStripper()
    out = "".join(s.feed(c) for c in ["<thi", "nk>gizli</th", "ink>{\"say\"", ": 1}"]) + s.flush()
    assert out == '{"say": 1}'


def test_quest_prompt_has_item_secret_and_it_rules():
    npc = {"personality": "x", "profile": {"name": "N"}}
    st = {"mission": {"your_turn": True, "advance_item_options": ["knows", "partial", "clueless"]}}
    sysmsg = build_messages("quest", npc, st, [], "selam")[0]["content"]
    for k in ("advance_item_options", "secret_for_player", "öğrenirsin", "install_mistakes", "knows"):
        assert k in sysmsg


def test_rules_keep_introductions_short():
    npc = {"personality": "x", "profile": {"name": "N"}}
    for ctx in ("shop", "street", "quest"):
        sysmsg = build_messages(ctx, npc, {}, [], "selam")[0]["content"]
        assert "SADECE ad, soyad ve ünvanını" in sysmsg and "Geveze olma" in sysmsg
