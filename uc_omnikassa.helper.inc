<?php
// vi:ft=php:fdm=indent

/**
 * Generate a data string from an array.
 */
function uc_omnikassa_makedata($array) {
  $data = '';

  foreach ($array as $key => $value) {
    $data .= "|$key=$value";
  }

  return substr($data, 1);
}

/** 
 * Generate a seal from a given data string.
 */
function uc_omnikassa_makeseal($data) {
  return hash('sha256', utf8_encode($data . variable_get('uc_omnikassa_key', '')));
}

