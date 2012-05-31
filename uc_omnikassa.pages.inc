<?php
// vi:ft=php:fdm=indent
//
require_once 'uc_omnikassa.helper.inc';

/**
 * The page that the user is returned to when completing the payment.
 */
function uc_omnikassa_complete() {
  // When an critical/security error occurs, we simply state that an error 
  // occured (without stating what), and log the details.
  function error($message)
  {
    drupal_set_message(t("An error has occured. Please try again."), 'error');
    watchdog('uc_omnikassa', t('Invalid OmniKassa payment received: @message', array('@message' => $message)), WATCHDOG_ERROR);
    drupal_goto('cart');
  }

  // If no data has been send, the user just navigated to this page for some 
  // reason. Redirect them to the home page.
  if (!isset($_POST['Data'])) {
    drupal_goto('<front>');
  }

  // Get the data.
  $data = $_POST['Data'];
  $seal = $_POST['Seal'];

  // Check the seal. If it's incorrect, give an error and abort.
  $seal2 = uc_omnikassa_makeseal($data);
  if ($seal != $seal2) {
    return error('invalid seal.');
  }

  // Extract the data.
  $data = uc_omnikassa_extractdata($data);

  // Check whether the order id matches the current session.
  $order_id = $_SESSION['cart_order'];
  if (!isset($_SESSION['cart_order']) || $order_id != $data['orderId']) {
    return error('order id that does not match the session.');
  }

  // Load the order info.
  $order = uc_order_load($order_id);
  if (!$order) {
    return error('non-existant order.');
  }

  // Check the order data against the payment data.
  if ((int)($order->order_total * 100) != (int)($data['amount'])) {
    return error("amount of the payment does not match the amount of the order: {$order->order_total}, {$data['amount']}");
  }

  // Check if the payment is a duplicate.
  $duplicate = (bool) db_query_range('SELECT 1 FROM {uc_payment_omnikassa} WHERE reference = :reference', 0, 1, array(':reference' => $data['transactionReference']))->fetchField();
  if ($duplicate) {
    watchdog('uc_omnikassa', 'OmniKassa transaction @reference has been processed before.', array('@reference' => $data['transactionReference']), WATCHDOG_NOTICE);
    return;
  }

  // Insert the payment into the database.
  db_insert('uc_payment_omnikassa')
    ->fields(array(
      'order_id'       => $order_id,
      'type'           => $data['paymentMeanType'],
      'brand'          => $data['paymentMeanBrand'],
      'ident'          => isset($data['maskedPan']) ? $data['maskedPan'] : '',
      'reference'      => $data['transactionReference'],
      'response'       => $data['responseCode'],
      'amount'         => $data['amount'],
      'processed'      => date_create_from_format('Y-m-d\TH:i:sP', $data['transactionDateTime'])->getTimeStamp(),
      'received'       => REQUEST_TIME,
    ))
    ->execute();

  // Check the payment status.
  switch ($data['responseCode']) {
    // Succes.
    case '00':
      uc_order_comment_save($order_id, 0, t('Payment of @amount @currency received through OmniKassa.', array('@amount' => uc_currency_format($order->order_total, FALSE), '@currency' => $order->currency)), 'order');
      uc_order_update_status($order_id, 'payment_received');
      uc_cart_complete_sale($order);

      // This lets us know it's a legitimate access of the complete page.
      $_SESSION['uc_checkout'][$_SESSION['cart_order']]['do_complete'] = TRUE;
      drupal_goto('cart/checkout/complete');
      break;

    // Payment pending. How to handle this gracefully?
    case '60':
      uc_order_update_status($order_id, 'pending');
      drupal_set_message(t('Your payment is pending.'), 'info');
      drupal_goto('cart');
      break;

    // Payment canceled by the user.
    case '17':
      drupal_set_message(t('Payment canceled.'), 'info');
      drupal_goto('cart');
      break;

    case '03':
    case '12':
    case '24':
    case '25':
    case '30':
    case '40':
    case '63':
    case '90':
    case '94':
    case '97':
    case '99':
      uc_order_comment_save($order_id, 0, t('An technical error occured while processing the payment of order @order_id: @error_code.', array('@order_id' => $order_id, '@error_code' => $data['responseCode'])));
      drupal_set_message(t('A technical error has occured while processing your payment. Please try again later.'), 'error');
      drupal_goto('cart');
      break;

    case '02':
    case '05':
    case '14':
    case '34':
    case '75':
      uc_order_comment_save($order_id, 0, t('The payment of order @order_id has been refused: @error_code.', array('@order_id' => $order_id, '@error_code' => $data['responseCode'])));
      drupal_set_message(t('Your payment has been canceled. Please try again later.'), 'error');
      drupal_goto('cart');
      break;
  }
}
