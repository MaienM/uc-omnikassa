<?php
// vi:ft=php:fdm=indent
//
require_once 'uc_omnikassa.helper.inc';

/**
 * The page that the user is returned to when completing the payment.
 */
function uc_omnikassa_complete() {
  // Create a link to an transaction.
  function _link($data) {
    return l(t('Details'), "admin/store/orders/{$data['orderId']}/transaction/{$data['transactionReference']}");
  }

  // When an critical/security error occurs, we simply state that an error 
  // occured (without stating what), and log the details.
  function error($data, $message) {
    drupal_set_message(t('An error has occured. Please try again.'), 'error');
    if ($data) {
      watchdog('uc_omnikassa', 'Invalid OmniKassa transaction @reference recieved for order @order: @message', array(
        '@message' => t($message),
        '@reference' => $data['reference'],
        '@order' => $data['orderId'],
      ), WATCHDOG_ERROR, _link($data));
      insert($data, $message, FALSE);
    }
    else {
      watchdog('uc_omnikassa', 'Invalid OmniKassa transaction received: @message', array('@message' => t($message)), WATCHDOG_ERROR);
    }
    drupal_goto('cart');
  }

  // Insert the payment into the database.
  function insert($data, $status, $accepted)
  {
    db_insert('uc_payment_omnikassa')
      ->fields(array(
        'order_id'  => $data['orderId'],
        'type'      => $data['paymentMeanType'],
        'brand'     => $data['paymentMeanBrand'],
        'ident'     => isset($data['maskedPan']) ? $data['maskedPan'] : '',
        'reference' => $data['transactionReference'],
        'response'  => $data['responseCode'],
        'amount'    => $data['amount'],
        'processed' => date_create_from_format(DateTime::ATOM, $data['transactionDateTime'])->getTimeStamp(),
        'received'  => REQUEST_TIME,
        'status'    => $status,
        'accepted'  => (int)$accepted,
      ))
      ->execute();
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
    return error(NULL, 'invalid seal.');
  }

  // Extract the data.
  $data = uc_omnikassa_extractdata($data);

  // Log the received data.
  watchdog('uc_omnikassa', 'OmniKassa transaction @reference recieved for order @order.', array(
    '@reference' => $data['transactionReference'],
    '@order'     => $data['orderId'],
  ), WATCHDOG_INFO, _link($data));

  // Check whether the order id matches the current session.
  $order_id = $_SESSION['cart_order'];
  if (!isset($_SESSION['cart_order']) || $order_id != $data['orderId']) {
    return error($data, 'order id that does not match the session.');
  }

  // Load the order info.
  $order = uc_order_load($order_id);
  if (!$order) {
    return error($data, 'non-existant order.');
  }

  // Check the order data against the payment data.
  if ((int)($order->order_total * 100) != (int)($data['amount'])) {
    return error($data, "amount of the payment does not match the amount of the order: {$order->order_total}, {$data['amount']}");
  }

  // Check if the payment is a duplicate.
  $duplicate = (bool) db_query_range('SELECT 1 FROM {uc_payment_omnikassa} WHERE reference = :reference', 0, 1, array(':reference' => $data['transactionReference']))->fetchField();
  if ($duplicate) {
    watchdog('uc_omnikassa', 'OmniKassa transaction @reference has been processed before.', array('@reference' => $data['transactionReference']), WATCHDOG_NOTICE);
    return;
  }

  // Store the payment in the database.
  insert($data, 'valid response', TRUE);

  // Check the payment status.
  switch ($data['responseCode']) {
    // Succes.
    case '00':
      uc_order_comment_save($order_id, 0, t('Payment of @amount @currency received through OmniKassa.', array('@amount' => uc_currency_format($order->order_total, FALSE), '@currency' => $order->currency)), 'order');
      uc_order_comment_save($order_id, 0, t('Payment of @amount @currency received through OmniKassa.', array('@amount' => uc_currency_format($order->order_total, FALSE), '@currency' => $order->currency)));
      uc_order_update_status($order_id, 'payment_received');
      uc_cart_complete_sale($order);

      // This lets us know it's a legitimate access of the complete page.
      $_SESSION['uc_checkout'][$_SESSION['cart_order']]['do_complete'] = TRUE;
      drupal_goto('cart/checkout/complete');
      break;

    // Payment pending. How to handle this gracefully?
    case '60':
      uc_order_update_status($order_id, 'omnikassa_pending');
      drupal_set_message(t('Your payment is pending.'), 'info');
      drupal_goto('cart');
      break;

    // Payment canceled by the user.
    case '17':
      drupal_set_message(t('Payment canceled.'), 'info');
      drupal_goto('cart');
      break;

    case '02':
    case '05':
    case '14':
    case '34':
    case '75':
      uc_order_comment_save($order_id, 0, t('The payment has been refused: @error_code.', array('@order_id' => $order_id, '@error_code' => $data['responseCode'])));
      drupal_set_message(t('Your payment has been canceled. Please try again later.'), 'error');
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
    default:
      uc_order_comment_save($order_id, 0, t('A technical error occured while processing the payment: @error_code.', array('@order_id' => $order_id, '@error_code' => $data['responseCode'])));
      drupal_set_message(t('A technical error has occured while processing your payment. Please try again later.'), 'error');
      drupal_goto('cart');
      break;
  }
}

/**
 * A table of all transactions (even rejected/invalid ones) for any given order.
 */
function uc_omnikassa_transactions($order_id) {
  // The table header.
  $header = array(
    NULL,
    t('Reference'),
    t('Status'),
    t('Type'),
    t('Ident'),
    t('Received')
  );

  // The table rows.
  $rows = array();
  $result = db_query("SELECT * FROM {uc_payment_omnikassa} WHERE order_id = :id ORDER BY received", array(':id' => $order_id));
  foreach ($result as $transaction) {
    $rows[] = array(
      'data' => array(
        '',
        l($transaction->reference, "admin/store/orders/{$order_id}/transactions/{$transaction->reference}"),
        $transaction->accepted ? uc_omnikassa_response($transaction->response) : $transaction->status,
        $transaction->brand,
        $transaction->ident ? $transaction->ident : '-',
        format_date($transaction->received, 'long'), 
      ),
      'valign' => 'top',
    );
  }

  $build['transactions'] = array(
    '#theme' => 'table',
    '#header' => $header,
    '#rows' => $rows,
    '#empty' => t('No transactions have been received for this order.'),
  );

  return $build;
}

/**
 * View details of a specific transaction.
 */
function uc_omnikassa_transaction_detail($order_id, $transaction) {
  // Check whether the order id matches the order id of the transaction.
  if ($order_id != $transaction->order_id) {
    return false;
  }

  // Helper function.
  function add($name, $value) {
    return array(
      array(
        'data' => t($name), 
        'header' => TRUE,
      ),
      $value,
    );
  }

  // Build the detail overview.
  $rows = array();
  $rows[] = add('Order ID', $transaction->order_id);
  $rows[] = add('Transaction reference', $transaction->reference);
  $rows[] = add('Payment Method', $transaction->brand);
  if ($transaction->ident) {
    $rows[] = add('Ident', $transaction->ident);
  }
  $rows[] = add('Amount', $transaction->amount/100);
  $rows[] = add('Omnikassa Response', uc_omnikassa_response($transaction->response));
  $rows[] = add('Processed (OmniKassa)', format_date($transaction->processed, 'custom', 'l, j F Y H:i:s'));
  $rows[] = add('Received (Local)', format_date($transaction->received, 'custom', 'l, j F Y H:i:s'));
  $rows[] = add('Accepted locally', $transaction->accepted ? t('Yes') : t('No, @message', 
    array('@message' => $transaction->status)
  ));
  
  $build['transaction_detail'] = array(
    '#theme' => 'table',
    '#rows' => $rows,
    '#attributes' => array('class' => array('dblog-event')),
  );
  return $build;
}
