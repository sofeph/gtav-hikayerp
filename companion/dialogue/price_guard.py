"""Keep generated classified-ad prices grounded in explicit source facts."""

import re

_PRICE = r"(?:\$\s*\d[\d.,]*|\d[\d.,]*\s*(?:dolar(?:ı|i)?|\$))"
_STATED_PRICE = re.compile(
    rf"\b(?:fiyat(?:ı|i)?|ücret(?:i)?|bedel(?:i)?)\s*(?::|=)?\s*({_PRICE})",
    re.IGNORECASE,
)
_PRICE_VALUE = re.compile(_PRICE, re.IGNORECASE)


def _digits(value):
    return ''.join(char for char in value if char.isdigit())


def stated_prices(topics):
    """Return canonical currency amounts explicitly labeled as a price in source topics."""
    found = {}
    for topic in topics:
        if not isinstance(topic, str):
            continue
        for match in _STATED_PRICE.finditer(topic):
            value = match.group(1).strip()
            normalized = _digits(value)
            if normalized:
                found[normalized] = value
    return found


def validated_ad_price(candidate, allowed_prices):
    """Keep only a generated amount that exactly matches an explicitly stated price."""
    if not isinstance(candidate, str) or not allowed_prices:
        return ''
    for match in _PRICE_VALUE.finditer(candidate):
        normalized = _digits(match.group(0))
        if normalized in allowed_prices:
            return allowed_prices[normalized]
    return ''
