'use strict';

const form = document.querySelector('#address-form');
const input = document.querySelector('#address');
const status = document.querySelector('#status');

form.addEventListener('submit', (event) => {
  event.preventDefault();
  const value = input.value.trim();
  if (!value) {
    status.textContent = 'Önce bir adres yaz.';
    input.focus();
    return;
  }
  const hasScheme = /^[a-z][a-z0-9+.-]*:\/\//i.test(value);
  const looksLikeHost = /^[^\s.]+\.[^\s]+(?:\/.*)?$/.test(value);
  const target = hasScheme ? value : looksLikeHost ? `https://${value}` : `https://www.google.com/search?q=${encodeURIComponent(value)}`;
  window.location.assign(target);
});
