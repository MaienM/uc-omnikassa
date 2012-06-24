<?php
// vi:ft=php:fdm=indent

/**
 * A mapping of response code -> description.
 */
function uc_omnikassa_response($code) {
  static $response = array(
    '00' => 'Transaction  success,  authorization  accepted  (transaction  succeeded, authorization accepted). ',
    '02' => 'Please phone the bank because the authorization limit on the card has been exceeded.',
    '03' => 'Invalid merchant contract.',
    '05' => 'Do not honor, authorization refused.',
    '12' => 'Invalid transaction, check the parameters sent in the request. ',
    '14' => 'Invalid card number or invalid Card Security Code or Card (for MasterCard) or invalid Card Verification Value (for Visa).',
    '17' => 'Cancellation of payment by the end user.',
    '24' => 'Invalid status. ',
    '25' => 'Transaction not found in database.',
    '30' => 'Invalid format.',
    '34' => 'Fraud suspicion.',
    '40' => 'Operation not allowed to this merchant.',
    '60' => 'Pending transaction.',
    '63' => 'Security breach detected, transaction stopped. ',
    '75' => 'The number of attempts to enter the card number has been exceeded (Three tries exhausted).',
    '90' => 'Acquirer server temporarily unavailable.',
    '94' => 'Duplicate transaction. (Transaction reference already reserved).',
    '97' => 'Request time-out; transaction refused.',
    '99' => 'Payment page temporarily unavailable.',
  );
  return $response[$code];
}

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
 * Generate an array from a data string. 
 *
 * The inverse of uc_omnikassa_makedata.
 */
function uc_omnikassa_extractdata($data) {
  $arr = array();

  foreach (explode('|', $data) as $d) {
    $d = explode('=', $d, 2);
    $arr[$d[0]] = $d[1];
  }

  return $arr;
}

/** 
 * Generate a seal from a given data string.
 */
function uc_omnikassa_makeseal($data) {
  return hash('sha256', utf8_encode($data . (variable_get('uc_omnikassa_test', TRUE) ? '002020000000001_KEY1' : variable_get('uc_omnikassa_key', ''))));
}

